#include "rsvp/lifecycle/reader_lifecycle.hpp"

#include "rsvp/storage/book_storage.hpp"

#include <algorithm>

namespace rsvp::lifecycle {
namespace {

rsvp::playback::PunctuationPauseMode to_playback_punctuation(
    rsvp::storage::PunctuationMode mode) {
    switch (mode) {
    case rsvp::storage::PunctuationMode::off:
        return rsvp::playback::PunctuationPauseMode::off;
    case rsvp::storage::PunctuationMode::gentle:
        return rsvp::playback::PunctuationPauseMode::gentle;
    }
    return rsvp::playback::PunctuationPauseMode::gentle;
}

rsvp::storage::PunctuationMode to_storage_punctuation(
    rsvp::playback::PunctuationPauseMode mode) {
    switch (mode) {
    case rsvp::playback::PunctuationPauseMode::off:
        return rsvp::storage::PunctuationMode::off;
    case rsvp::playback::PunctuationPauseMode::gentle:
        return rsvp::storage::PunctuationMode::gentle;
    }
    return rsvp::storage::PunctuationMode::gentle;
}

} // namespace

rsvp::playback::PlaybackSettings to_playback_settings(
    const rsvp::storage::SettingsV1& settings) {
    rsvp::playback::PlaybackSettings playback;
    playback.wpm = settings.wpm;
    playback.punctuation_pause_mode = to_playback_punctuation(settings.punctuation_mode);
    return playback;
}

rsvp::storage::SettingsV1 settings_from_playback(
    const rsvp::playback::PlaybackSettings& playback_settings) {
    rsvp::storage::SettingsV1 settings;
    settings.wpm = playback_settings.wpm;
    settings.punctuation_mode =
        to_storage_punctuation(playback_settings.punctuation_pause_mode);
    return settings;
}

rsvp::storage::BookmarkV1 bookmark_from_display_state(
    const DocumentIdentity& document,
    const rsvp::playback::DisplayState& state,
    std::uint64_t updated_at) {
    rsvp::storage::BookmarkV1 bookmark;
    bookmark.doc_id = document.doc_id;
    bookmark.content_hash = document.content_hash;
    bookmark.token_index = state.frame_index;
    bookmark.frame_index = state.frame_index;
    bookmark.source_token_index = state.source_token_index;
    bookmark.chunk_index = state.chunk_index;
    bookmark.source_byte_offset =
        state.visible_frame ? state.visible_frame->source_byte_start : 0;
    bookmark.updated_at = updated_at;
    return bookmark;
}

CapturedReaderState capture_reader_state(
    const DocumentIdentity& document,
    const rsvp::playback::DisplayState& state,
    const rsvp::playback::PlaybackSettings& playback_settings,
    std::uint64_t updated_at) {
    return {settings_from_playback(playback_settings),
            bookmark_from_display_state(document, state, updated_at)};
}

CapturedReaderState capture_reader_state(
    const DocumentIdentity& document,
    const rsvp::controller::ReaderController& controller,
    std::uint64_t updated_at) {
    return capture_reader_state(document,
                                controller.playback().snapshot(),
                                controller.playback().settings(),
                                updated_at);
}

bool bookmark_matches_document(const rsvp::storage::BookmarkV1& bookmark,
                               const DocumentIdentity& document) {
    return bookmark.doc_id == document.doc_id &&
           bookmark.content_hash == document.content_hash;
}

std::filesystem::path resume_path_for_document(
    const ReaderStatePaths& paths,
    std::string_view doc_id) {
    if (!paths.resume_dir.empty()) {
        return paths.resume_dir /
               (rsvp::storage::sanitize_storage_key(doc_id) + ".state");
    }
    return paths.bookmark_path;
}

std::filesystem::path resume_path_for_document(
    const ReaderStatePaths& paths,
    const DocumentIdentity& document) {
    return resume_path_for_document(paths, document.doc_id);
}

std::size_t resolve_bookmark_frame(
    const rsvp::storage::BookmarkV1& bookmark,
    const std::vector<rsvp::text::DisplayFrame>& frames) {
    if (bookmark.frame_index < frames.size()) {
        const auto& frame = frames[bookmark.frame_index];
        if (frame.source_token_index == bookmark.source_token_index &&
            frame.chunk_index == bookmark.chunk_index) {
            return bookmark.frame_index;
        }
    }

    for (std::size_t index = 0; index < frames.size(); ++index) {
        const auto& frame = frames[index];
        if (frame.source_token_index == bookmark.source_token_index &&
            frame.chunk_index == bookmark.chunk_index) {
            return index;
        }
    }

    for (std::size_t index = 0; index < frames.size(); ++index) {
        if (frames[index].source_byte_start >= bookmark.source_byte_offset) {
            return index;
        }
    }

    return frames.empty() ? std::size_t{0} : frames.size() - 1;
}

bool persist_reader_state(const ReaderStatePaths& paths,
                          const CapturedReaderState& state) {
    return rsvp::storage::save_settings_atomic(paths.settings_path, state.settings) &&
           rsvp::storage::save_bookmark_atomic(
               resume_path_for_document(paths, state.bookmark.doc_id),
               state.bookmark);
}

bool pause_and_persist_for_sleep(rsvp::playback::DisplayAdapter& adapter,
                                 const ReaderStatePaths& paths,
                                 const DocumentIdentity& document,
                                 std::uint64_t updated_at) {
    adapter.pause();
    return persist_reader_state(
        paths,
        capture_reader_state(document, adapter.snapshot(), adapter.settings(), updated_at));
}

bool pause_and_persist_for_sleep(rsvp::controller::ReaderController& controller,
                                 const ReaderStatePaths& paths,
                                 const DocumentIdentity& document,
                                 std::uint64_t updated_at) {
    controller.handle_input(rsvp::controller::ReaderInput::sleep, updated_at);
    return persist_reader_state(paths, capture_reader_state(document, controller, updated_at));
}

rsvp::controller::ReaderController restore_reader_controller(
    const std::vector<rsvp::text::DisplayFrame>& frames,
    const std::vector<rsvp::text::Chapter>& chapters,
    const ReaderStatePaths& paths,
    const DocumentIdentity& document,
    rsvp::playback::PlaybackSettings fallback_settings,
    std::uint64_t monotonic_ms) {
    const auto loaded_settings = rsvp::storage::load_settings(paths.settings_path);
    auto controller = rsvp::controller::ReaderController(
        frames,
        chapters,
        loaded_settings ? to_playback_settings(*loaded_settings) : fallback_settings);

    auto loaded_bookmark =
        rsvp::storage::load_bookmark(resume_path_for_document(paths, document));
    if (!loaded_bookmark && !paths.bookmark_path.empty()) {
        loaded_bookmark = rsvp::storage::load_bookmark(paths.bookmark_path);
    }
    if (loaded_bookmark && bookmark_matches_document(*loaded_bookmark, document)) {
        controller.restore_paused(resolve_bookmark_frame(*loaded_bookmark, frames),
                                  monotonic_ms,
                                  rsvp::controller::ReaderDisplayMode::title);
    }
    return controller;
}

} // namespace rsvp::lifecycle

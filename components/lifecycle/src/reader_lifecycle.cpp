#include "rsvp/lifecycle/reader_lifecycle.hpp"

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

bool bookmark_matches_document(const rsvp::storage::BookmarkV1& bookmark,
                               const DocumentIdentity& document) {
    return bookmark.doc_id == document.doc_id &&
           bookmark.content_hash == document.content_hash;
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
           rsvp::storage::save_bookmark_atomic(paths.bookmark_path, state.bookmark);
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

} // namespace rsvp::lifecycle

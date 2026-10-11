#pragma once

#include "rsvp/controller/reader_controller.hpp"
#include "rsvp/playback/display_adapter.hpp"
#include "rsvp/storage/state_store.hpp"
#include "rsvp/text/display_frame.hpp"
#include "rsvp/text/chapter.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace rsvp::lifecycle {

struct DocumentIdentity {
    std::string doc_id;
    std::uint64_t content_hash = 0;
};

struct ReaderStatePaths {
    std::filesystem::path settings_path;
    std::filesystem::path bookmark_path;
    std::filesystem::path resume_dir;
};

struct CapturedReaderState {
    rsvp::storage::SettingsV1 settings;
    rsvp::storage::BookmarkV1 bookmark;
};

rsvp::playback::PlaybackSettings to_playback_settings(
    const rsvp::storage::SettingsV1& settings);
rsvp::storage::SettingsV1 settings_from_playback(
    const rsvp::playback::PlaybackSettings& playback_settings);

rsvp::storage::BookmarkV1 bookmark_from_display_state(
    const DocumentIdentity& document,
    const rsvp::playback::DisplayState& state,
    std::uint64_t updated_at);

CapturedReaderState capture_reader_state(
    const DocumentIdentity& document,
    const rsvp::playback::DisplayState& state,
    const rsvp::playback::PlaybackSettings& playback_settings,
    std::uint64_t updated_at);

CapturedReaderState capture_reader_state(
    const DocumentIdentity& document,
    const rsvp::controller::ReaderController& controller,
    std::uint64_t updated_at);

bool bookmark_matches_document(const rsvp::storage::BookmarkV1& bookmark,
                               const DocumentIdentity& document);

std::filesystem::path resume_path_for_document(
    const ReaderStatePaths& paths,
    std::string_view doc_id);

std::filesystem::path resume_path_for_document(
    const ReaderStatePaths& paths,
    const DocumentIdentity& document);

std::size_t resolve_bookmark_frame(
    const rsvp::storage::BookmarkV1& bookmark,
    const std::vector<rsvp::text::DisplayFrame>& frames);

bool persist_reader_state(const ReaderStatePaths& paths,
                          const CapturedReaderState& state);

bool pause_and_persist_for_sleep(rsvp::playback::DisplayAdapter& adapter,
                                 const ReaderStatePaths& paths,
                                 const DocumentIdentity& document,
                                 std::uint64_t updated_at);

bool pause_and_persist_for_sleep(rsvp::controller::ReaderController& controller,
                                 const ReaderStatePaths& paths,
                                 const DocumentIdentity& document,
                                 std::uint64_t updated_at);

rsvp::controller::ReaderController restore_reader_controller(
    const std::vector<rsvp::text::DisplayFrame>& frames,
    const std::vector<rsvp::text::Chapter>& chapters,
    const ReaderStatePaths& paths,
    const DocumentIdentity& document,
    rsvp::playback::PlaybackSettings fallback_settings = {},
    std::uint64_t monotonic_ms = 0);

} // namespace rsvp::lifecycle

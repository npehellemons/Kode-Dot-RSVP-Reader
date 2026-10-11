#pragma once

#include "rsvp/playback/playback_engine.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace rsvp::controller {

enum class ReaderDisplayMode {
    title,
    rsvp,
    context,
    sleep,
};

enum class ReaderInput {
    red,
    blue,
    dpad_left,
    dpad_right,
    dpad_up,
    dpad_down,
    hold_left,
    hold_right,
    hold_end,
    sleep,
};

struct ReaderView {
    ReaderDisplayMode mode = ReaderDisplayMode::title;
    rsvp::playback::DisplayState playback;
    rsvp::playback::SentenceContextWindow context;
};

class ReaderController {
public:
    ReaderController(std::vector<rsvp::text::DisplayFrame> frames,
                     std::vector<rsvp::text::Chapter> chapters = {},
                     rsvp::playback::PlaybackSettings settings = {});

    ReaderView view() const;
    const rsvp::playback::PlaybackEngine& playback() const;

    bool handle_input(ReaderInput input, std::uint64_t monotonic_ms);
    bool tick(std::uint64_t monotonic_ms);
    void restore_paused(std::size_t frame_index,
                        std::uint64_t monotonic_ms,
                        ReaderDisplayMode mode = ReaderDisplayMode::title);

private:
    bool toggle_playback(std::uint64_t monotonic_ms);
    void show_rsvp();
    void show_context();

    rsvp::playback::PlaybackEngine playback_;
    ReaderDisplayMode mode_ = ReaderDisplayMode::title;
};

} // namespace rsvp::controller

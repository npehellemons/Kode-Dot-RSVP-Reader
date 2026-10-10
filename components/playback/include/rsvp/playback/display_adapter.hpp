#pragma once

#include "rsvp/playback/playback_engine.hpp"

#include <functional>
#include <vector>

namespace rsvp::playback {

class DisplayAdapter {
public:
    using Callback = std::function<void(const DisplayState&)>;

    DisplayAdapter(std::vector<rsvp::text::Token> tokens,
                   PlaybackSettings settings,
                   Callback callback);
    DisplayAdapter(std::vector<rsvp::text::DisplayFrame> frames,
                   PlaybackSettings settings,
                   Callback callback);
    DisplayAdapter(std::vector<rsvp::text::DisplayFrame> frames,
                   std::vector<rsvp::text::Chapter> chapters,
                   PlaybackSettings settings,
                   Callback callback);

    const DisplayState& snapshot() const;
    const PlaybackSettings& settings() const;

    bool emit_current();
    bool start(std::uint64_t monotonic_ms);
    bool pause();
    bool resume(std::uint64_t monotonic_ms);
    bool restart(std::uint64_t monotonic_ms);
    bool seek(std::size_t token_index, std::uint64_t monotonic_ms);
    bool rewind_one(std::uint64_t monotonic_ms);
    bool advance_one(std::uint64_t monotonic_ms);
    bool rewind_word(std::uint64_t monotonic_ms);
    bool advance_word(std::uint64_t monotonic_ms);
    bool rewind_sentence(std::uint64_t monotonic_ms);
    bool set_wpm(int wpm);
    bool tick(std::uint64_t monotonic_ms);

private:
    bool emit_if_changed();
    bool differs_from_last_emit(const DisplayState& state) const;

    PlaybackEngine engine_;
    Callback callback_;
    DisplayState last_emitted_;
    bool has_emitted_ = false;
};

} // namespace rsvp::playback

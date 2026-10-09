#pragma once

#include "rsvp/text/tokenizer.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace rsvp::playback {

enum class PlaybackState {
    idle,
    playing,
    paused,
    finished,
};

enum class PunctuationPauseMode {
    off,
    gentle,
};

struct PlaybackSettings {
    int wpm = 250;
    PunctuationPauseMode punctuation_pause_mode = PunctuationPauseMode::gentle;
};

struct DisplayState {
    PlaybackState state = PlaybackState::idle;
    std::size_t token_index = 0;
    std::size_t token_count = 0;
    std::optional<rsvp::text::Token> visible_token;
    std::uint64_t estimated_remaining_ms = 0;
};

class PlaybackEngine {
public:
    explicit PlaybackEngine(std::vector<rsvp::text::Token> tokens,
                            PlaybackSettings settings = {});

    const DisplayState& snapshot() const;
    const PlaybackSettings& settings() const;

    void start(std::uint64_t monotonic_ms);
    void pause();
    void resume(std::uint64_t monotonic_ms);
    void restart(std::uint64_t monotonic_ms);
    void seek(std::size_t token_index, std::uint64_t monotonic_ms);
    void rewind_one(std::uint64_t monotonic_ms);
    void advance_one(std::uint64_t monotonic_ms);
    bool set_wpm(int wpm);
    void set_punctuation_pause_mode(PunctuationPauseMode mode);

    bool tick(std::uint64_t monotonic_ms);
    std::uint64_t duration_for_index(std::size_t token_index) const;

    static constexpr int kMinWpm = 100;
    static constexpr int kMaxWpm = 600;

private:
    std::uint64_t base_duration_ms() const;
    void update_visible_token();
    void update_remaining();
    void schedule_next_from(std::uint64_t monotonic_ms);
    void finish();

    std::vector<rsvp::text::Token> tokens_;
    PlaybackSettings settings_;
    DisplayState snapshot_;
    std::uint64_t next_due_ms_ = 0;
};

} // namespace rsvp::playback

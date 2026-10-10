#pragma once

#include "rsvp/text/chapter.hpp"
#include "rsvp/text/display_frame.hpp"
#include "rsvp/text/tokenizer.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
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
    std::size_t frame_index = 0;
    std::size_t frame_count = 0;
    std::size_t source_token_index = 0;
    std::size_t chunk_index = 0;
    std::size_t chunk_count = 1;
    std::size_t chapter_index = 0;
    std::size_t chapter_count = 0;
    std::size_t chapter_frame_index = 0;
    std::size_t chapter_frame_count = 0;
    std::string chapter_title;
    int wpm = 250;
    std::optional<rsvp::text::Token> visible_token;
    std::optional<rsvp::text::DisplayFrame> visible_frame;
    std::uint64_t estimated_remaining_ms = 0;
    std::uint64_t estimated_chapter_remaining_ms = 0;
};

struct SentenceContextWindow {
    std::size_t start_frame_index = 0;
    std::size_t end_frame_index = 0;
    std::size_t current_frame_index = 0;
    std::size_t current_source_token_index = 0;
};

struct FrameNavigation {
    std::size_t previous_word_frame_index = 0;
    std::size_t next_word_frame_index = 0;
    std::size_t sentence_rewind_frame_index = 0;
    bool starts_chapter = false;
};

class PlaybackEngine {
public:
    explicit PlaybackEngine(std::vector<rsvp::text::Token> tokens,
                            PlaybackSettings settings = {});
    explicit PlaybackEngine(std::vector<rsvp::text::DisplayFrame> frames,
                            PlaybackSettings settings = {});
    PlaybackEngine(std::vector<rsvp::text::DisplayFrame> frames,
                   std::vector<rsvp::text::Chapter> chapters,
                   PlaybackSettings settings = {});

    const DisplayState& snapshot() const;
    const PlaybackSettings& settings() const;

    void start(std::uint64_t monotonic_ms);
    void pause();
    void resume(std::uint64_t monotonic_ms);
    void restart(std::uint64_t monotonic_ms);
    void seek(std::size_t frame_index, std::uint64_t monotonic_ms);
    void rewind_one(std::uint64_t monotonic_ms);
    void advance_one(std::uint64_t monotonic_ms);
    void rewind_word(std::uint64_t monotonic_ms);
    void advance_word(std::uint64_t monotonic_ms);
    void rewind_sentence(std::uint64_t monotonic_ms);
    bool set_wpm(int wpm);
    void set_punctuation_pause_mode(PunctuationPauseMode mode);

    bool tick(std::uint64_t monotonic_ms);
    std::uint64_t duration_for_index(std::size_t frame_index) const;
    SentenceContextWindow sentence_context_for_frame(std::size_t frame_index) const;
    SentenceContextWindow current_sentence_context() const;
    FrameNavigation navigation_for_frame(std::size_t frame_index) const;

    static constexpr int kMinWpm = 100;
    static constexpr int kMaxWpm = 600;

private:
    std::uint64_t base_duration_ms() const;
    void seek_and_pause(std::size_t frame_index, std::uint64_t monotonic_ms);
    std::size_t first_frame_for_source_token(std::size_t source_token_index) const;
    std::size_t previous_word_frame_for_frame(std::size_t frame_index) const;
    std::size_t next_word_frame_for_frame(std::size_t frame_index) const;
    std::size_t sentence_rewind_frame_for_frame(std::size_t frame_index) const;
    std::size_t previous_word_frame() const;
    std::size_t next_word_frame() const;
    std::size_t sentence_start_for_frame(std::size_t frame_index) const;
    std::size_t sentence_end_for_frame(std::size_t frame_index) const;
    std::size_t previous_sentence_start_before(std::size_t frame_index) const;
    std::size_t chapter_index_for_frame(std::size_t frame_index) const;
    void update_visible_token();
    void update_remaining();
    void schedule_next_from(std::uint64_t monotonic_ms);
    void finish();

    std::vector<rsvp::text::DisplayFrame> frames_;
    std::vector<rsvp::text::Chapter> chapters_;
    PlaybackSettings settings_;
    DisplayState snapshot_;
    std::uint64_t next_due_ms_ = 0;
};

} // namespace rsvp::playback

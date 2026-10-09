#include "rsvp/playback/playback_engine.hpp"
#include "rsvp/text/tokenizer.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

using rsvp::playback::PlaybackEngine;
using rsvp::playback::PlaybackSettings;
using rsvp::playback::PlaybackState;
using rsvp::playback::PunctuationPauseMode;
using rsvp::text::PunctuationFlags;
using rsvp::text::has_flag;
using rsvp::text::tokenize;

int failures = 0;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void test_basic_tokenization_offsets_and_flags() {
    const std::string input = "  Hello, world!\n\nNext line";
    const auto tokens = tokenize(input);

    require(tokens.size() == 4, "basic fixture token count");
    require(tokens[0].text_utf8 == "Hello,", "first token text");
    require(tokens[0].source_byte_start == 2, "first token byte start");
    require(tokens[0].source_byte_length == 6, "first token byte length");
    require(has_flag(tokens[0].punctuation_flags, PunctuationFlags::comma),
            "comma flag detected");

    require(tokens[1].text_utf8 == "world!", "sentence token text");
    require(tokens[1].source_byte_start == 9, "sentence token byte start");
    require(has_flag(tokens[1].punctuation_flags, PunctuationFlags::sentence_end),
            "sentence end flag detected");

    require(tokens[2].text_utf8 == "Next", "paragraph token text");
    require(has_flag(tokens[2].punctuation_flags, PunctuationFlags::paragraph_break_before),
            "paragraph break before token detected");
}

void test_utf8_boundaries_and_invalid_bytes() {
    const std::string input = std::string("Cafe ") + "\xC3\xA9" + " \xFF done";
    const auto tokens = tokenize(input);

    require(tokens.size() == 4, "utf8 fixture token count");
    require(tokens[1].text_utf8 == "\xC3\xA9", "valid utf8 token preserved");
    require(tokens[1].source_byte_start == 5, "valid utf8 byte start");
    require(tokens[1].source_byte_length == 2, "valid utf8 byte length");
    require(tokens[2].text_utf8 == "\xEF\xBF\xBD", "invalid byte replacement token");
    require(tokens[2].source_byte_start == 8, "invalid byte original offset");
    require(tokens[2].source_byte_length == 1, "invalid byte original length");
    require(has_flag(tokens[2].punctuation_flags, PunctuationFlags::invalid_utf8),
            "invalid utf8 flag detected");
}

void test_empty_and_whitespace_input() {
    require(tokenize("").empty(), "empty input has no tokens");
    require(tokenize(" \t\n\r\n ").empty(), "whitespace input has no tokens");
}

void test_playback_exact_deadlines_and_pause() {
    auto tokens = tokenize("one two three");
    PlaybackEngine engine(tokens, PlaybackSettings{.wpm = 300});

    require(engine.snapshot().state == PlaybackState::idle, "initial state idle");
    require(engine.snapshot().visible_token->text_utf8 == "one", "initial visible token");

    engine.start(1000);
    require(engine.snapshot().state == PlaybackState::playing, "start enters playing");
    require(!engine.tick(1199), "tick before deadline does not advance");
    require(engine.snapshot().token_index == 0, "cursor unchanged before deadline");
    require(engine.tick(1200), "tick at deadline advances");
    require(engine.snapshot().token_index == 1, "cursor advanced once");
    require(engine.snapshot().visible_token->text_utf8 == "two", "second token visible");

    engine.pause();
    require(engine.snapshot().state == PlaybackState::paused, "pause enters paused");
    require(!engine.tick(5000), "paused tick does not advance");
    require(engine.snapshot().token_index == 1, "cursor frozen while paused");
    engine.resume(5000);
    require(!engine.tick(5199), "resume reschedules from resume time");
    require(engine.tick(5200), "resume advances at next boundary");
    require(engine.snapshot().visible_token->text_utf8 == "three", "third token visible");
}

void test_playback_overslept_tick_only_advances_once() {
    PlaybackEngine engine(tokenize("one two three four"), PlaybackSettings{.wpm = 300});
    engine.start(0);

    require(engine.tick(10000), "overslept tick advances");
    require(engine.snapshot().token_index == 1, "overslept tick advances one token only");
    require(engine.snapshot().visible_token->text_utf8 == "two", "overslept visible token");
}

void test_seek_rewind_advance_and_finish() {
    PlaybackEngine engine(tokenize("one two three"), PlaybackSettings{.wpm = 300});
    engine.start(0);
    engine.seek(99, 0);
    require(engine.snapshot().token_index == 2, "seek clamps high index");
    engine.rewind_one(0);
    require(engine.snapshot().token_index == 1, "rewind moves back");
    engine.advance_one(0);
    require(engine.snapshot().token_index == 2, "advance moves forward");
    engine.advance_one(0);
    require(engine.snapshot().state == PlaybackState::finished, "advance at end finishes");
}

void test_wpm_bounds_and_punctuation_durations() {
    PlaybackEngine engine(tokenize("Wait, stop. go"), PlaybackSettings{.wpm = 400});

    require(engine.duration_for_index(0) == 225, "comma duration is 1.5x rounded up");
    require(engine.duration_for_index(1) == 300, "sentence duration is 2x");
    require(engine.duration_for_index(2) == 150, "plain duration is base");

    require(!engine.set_wpm(50), "low wpm rejected");
    require(engine.settings().wpm == PlaybackEngine::kMinWpm, "low wpm clamps to min");
    require(!engine.set_wpm(1000), "high wpm rejected");
    require(engine.settings().wpm == PlaybackEngine::kMaxWpm, "high wpm clamps to max");

    engine.set_punctuation_pause_mode(PunctuationPauseMode::off);
    require(engine.duration_for_index(0) == 100, "punctuation off uses base duration");
}

} // namespace

int main() {
    test_basic_tokenization_offsets_and_flags();
    test_utf8_boundaries_and_invalid_bytes();
    test_empty_and_whitespace_input();
    test_playback_exact_deadlines_and_pause();
    test_playback_overslept_tick_only_advances_once();
    test_seek_rewind_advance_and_finish();
    test_wpm_bounds_and_punctuation_durations();

    if (failures != 0) {
        std::cerr << failures << " test failure(s)\n";
        return EXIT_FAILURE;
    }
    std::cout << "All RSVP core tests passed\n";
    return EXIT_SUCCESS;
}

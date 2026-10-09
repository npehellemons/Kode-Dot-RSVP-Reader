#include "rsvp/playback/display_adapter.hpp"
#include "rsvp/playback/playback_engine.hpp"
#include "rsvp/storage/state_store.hpp"
#include "rsvp/text/tokenizer.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

using rsvp::playback::PlaybackEngine;
using rsvp::playback::PlaybackSettings;
using rsvp::playback::PlaybackState;
using rsvp::playback::PunctuationPauseMode;
using rsvp::playback::DisplayAdapter;
using rsvp::playback::DisplayState;
using rsvp::storage::BookmarkV1;
using rsvp::storage::FixationMode;
using rsvp::storage::PunctuationMode;
using rsvp::storage::SettingsV1;
using rsvp::storage::content_hash;
using rsvp::storage::load_bookmark;
using rsvp::storage::load_settings;
using rsvp::storage::parse_bookmark;
using rsvp::storage::parse_hex_u64;
using rsvp::storage::parse_settings;
using rsvp::storage::save_bookmark_atomic;
using rsvp::storage::save_settings_atomic;
using rsvp::storage::serialize_bookmark;
using rsvp::storage::serialize_settings;
using rsvp::storage::to_hex;
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

void test_display_adapter_emits_only_changed_states() {
    std::vector<DisplayState> events;
    DisplayAdapter adapter(
        tokenize("one two three"), PlaybackSettings{.wpm = 300},
        [&events](const DisplayState& state) { events.push_back(state); });

    require(adapter.emit_current(), "initial display event emitted");
    require(events.size() == 1, "one initial event");
    require(events.back().visible_token->text_utf8 == "one", "initial event token");
    require(!adapter.emit_current(), "unchanged current state is suppressed");
    require(events.size() == 1, "unchanged emit does not add event");

    require(adapter.start(0), "start emits playing state");
    require(events.size() == 2, "start event count");
    require(events.back().state == PlaybackState::playing, "start event state");

    require(!adapter.tick(199), "early tick is suppressed");
    require(events.size() == 2, "early tick event count");

    require(adapter.tick(200), "deadline tick emits next token");
    require(events.size() == 3, "deadline tick event count");
    require(events.back().token_index == 1, "deadline tick token index");
    require(events.back().visible_token->text_utf8 == "two", "deadline tick token");

    require(adapter.tick(10000), "overslept tick emits one next token");
    require(events.size() == 4, "overslept tick event count");
    require(events.back().token_index == 2, "overslept tick advances only once");
    require(!adapter.tick(10000), "repeated same-time tick is suppressed");
    require(events.size() == 4, "same-time tick event count");
}

void test_display_adapter_emits_settings_and_manual_changes() {
    std::vector<DisplayState> events;
    DisplayAdapter adapter(
        tokenize("one two"), PlaybackSettings{.wpm = 250},
        [&events](const DisplayState& state) { events.push_back(state); });

    adapter.emit_current();
    require(events.back().wpm == 250, "display state includes initial wpm");

    require(adapter.set_wpm(350), "wpm change emits");
    require(events.back().wpm == 350, "display event has changed wpm");

    adapter.start(0);
    require(adapter.pause(), "pause emits");
    require(events.back().state == PlaybackState::paused, "pause event state");
    require(!adapter.pause(), "duplicate pause is suppressed");
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

std::filesystem::path temp_test_dir(const char* name) {
    const auto dir = std::filesystem::temp_directory_path() /
                     (std::string("kode-rsvp-") + name);
    std::error_code error;
    std::filesystem::remove_all(dir, error);
    std::filesystem::create_directories(dir, error);
    require(!error, "temp directory created");
    return dir;
}

void test_settings_serialization_and_atomic_load() {
    SettingsV1 settings;
    settings.wpm = 325;
    settings.font_scale = 115;
    settings.fixation_mode = FixationMode::orp;
    settings.punctuation_mode = PunctuationMode::off;

    const auto parsed = parse_settings(serialize_settings(settings));
    require(parsed.has_value(), "settings round trip parses");
    require(parsed->schema_version == SettingsV1::kSchemaVersion, "settings schema retained");
    require(parsed->wpm == 325, "settings wpm round trip");
    require(parsed->font_scale == 115, "settings font scale round trip");
    require(parsed->fixation_mode == FixationMode::orp, "settings fixation round trip");
    require(parsed->punctuation_mode == PunctuationMode::off,
            "settings punctuation round trip");

    const auto dir = temp_test_dir("settings");
    const auto path = dir / "settings.state";
    require(save_settings_atomic(path, settings), "settings atomic save succeeds");
    const auto loaded = load_settings(path);
    require(loaded.has_value(), "settings atomic load succeeds");
    require(loaded->wpm == 325, "loaded settings wpm");
    require(!std::filesystem::exists(path.string() + ".tmp"), "settings temp file removed");

    {
        std::ofstream corrupt(path, std::ios::binary | std::ios::trunc);
        corrupt << "schema_version=99\nwpm=325\n";
    }
    require(!load_settings(path).has_value(), "corrupt settings are rejected");

    std::error_code error;
    std::filesystem::remove_all(dir, error);
}

void test_bookmark_serialization_hash_and_atomic_load() {
    const auto hash = content_hash("sample document");
    require(to_hex(hash).size() == 16, "hash hex width");
    require(parse_hex_u64(to_hex(hash)).value_or(0) == hash, "hash hex parses");
    require(!parse_hex_u64("not-a-hash").has_value(), "bad hash rejected");

    BookmarkV1 bookmark;
    bookmark.doc_id = "bundled-sample";
    bookmark.content_hash = hash;
    bookmark.source_byte_offset = 42;
    bookmark.token_index = 7;
    bookmark.updated_at = 123456;

    const auto parsed = parse_bookmark(serialize_bookmark(bookmark));
    require(parsed.has_value(), "bookmark round trip parses");
    require(parsed->doc_id == "bundled-sample", "bookmark doc id round trip");
    require(parsed->content_hash == hash, "bookmark hash round trip");
    require(parsed->source_byte_offset == 42, "bookmark source offset round trip");
    require(parsed->token_index == 7, "bookmark token index round trip");
    require(parsed->updated_at == 123456, "bookmark updated time round trip");

    const auto dir = temp_test_dir("bookmark");
    const auto path = dir / "bookmark.state";
    require(save_bookmark_atomic(path, bookmark), "bookmark atomic save succeeds");
    const auto loaded = load_bookmark(path);
    require(loaded.has_value(), "bookmark atomic load succeeds");
    require(loaded->token_index == 7, "loaded bookmark token index");
    require(!std::filesystem::exists(path.string() + ".tmp"), "bookmark temp file removed");

    {
        std::ofstream corrupt(path, std::ios::binary | std::ios::trunc);
        corrupt << "schema_version=1\ncontent_hash=" << to_hex(hash) << '\n';
    }
    require(!load_bookmark(path).has_value(), "corrupt bookmark is rejected");

    std::error_code error;
    std::filesystem::remove_all(dir, error);
}

} // namespace

int main() {
    test_basic_tokenization_offsets_and_flags();
    test_utf8_boundaries_and_invalid_bytes();
    test_empty_and_whitespace_input();
    test_playback_exact_deadlines_and_pause();
    test_display_adapter_emits_only_changed_states();
    test_display_adapter_emits_settings_and_manual_changes();
    test_playback_overslept_tick_only_advances_once();
    test_seek_rewind_advance_and_finish();
    test_wpm_bounds_and_punctuation_durations();
    test_settings_serialization_and_atomic_load();
    test_bookmark_serialization_hash_and_atomic_load();

    if (failures != 0) {
        std::cerr << failures << " test failure(s)\n";
        return EXIT_FAILURE;
    }
    std::cout << "All RSVP core tests passed\n";
    return EXIT_SUCCESS;
}

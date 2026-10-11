#include "rsvp/controller/reader_controller.hpp"
#include "rsvp/lifecycle/reader_lifecycle.hpp"
#include "rsvp/library/document_text.hpp"
#include "rsvp/playback/display_adapter.hpp"
#include "rsvp/playback/playback_engine.hpp"
#include "rsvp/platform/input_contract.hpp"
#include "rsvp/storage/state_store.hpp"
#include "rsvp/text/display_frame.hpp"
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
using rsvp::lifecycle::DocumentIdentity;
using rsvp::lifecycle::ReaderStatePaths;
using rsvp::lifecycle::bookmark_matches_document;
using rsvp::lifecycle::capture_reader_state;
using rsvp::lifecycle::pause_and_persist_for_sleep;
using rsvp::lifecycle::resolve_bookmark_frame;
using rsvp::lifecycle::restore_reader_controller;
using rsvp::lifecycle::to_playback_settings;
using rsvp::library::ChapterSource;
using rsvp::library::DocumentFormat;
using rsvp::library::MetadataConfidence;
using rsvp::library::TxtImportOptions;
using rsvp::library::import_txt_document;
using rsvp::library::make_frame_chapters;
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
using rsvp::text::DisplayFrameOptions;
using rsvp::text::Chapter;
using rsvp::text::PunctuationFlags;
using rsvp::text::centered_index_for_codepoints;
using rsvp::text::make_chapters_from_markdown_headings;
using rsvp::text::make_display_frames;
using rsvp::text::orp_index_for_codepoints;
using rsvp::text::has_flag;
using rsvp::text::tokenize;
using rsvp::text::utf8_codepoint_count;
using rsvp::controller::ReaderController;
using rsvp::controller::ReaderDisplayMode;
using rsvp::controller::ReaderInput;
using rsvp::platform::ControlEvent;
using rsvp::platform::ControlPhase;
using rsvp::platform::LifecycleEvent;
using rsvp::platform::LifecycleSignal;
using rsvp::platform::PhysicalControl;
using rsvp::platform::reader_input_for_control_event;
using rsvp::platform::reader_input_for_lifecycle_event;

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

void test_display_frame_orp_indexes() {
    require(orp_index_for_codepoints(0) == 0, "empty ORP index");
    require(orp_index_for_codepoints(1) == 0, "one-letter ORP index");
    require(orp_index_for_codepoints(2) == 1, "two-letter ORP index");
    require(orp_index_for_codepoints(5) == 1, "five-letter ORP index");
    require(orp_index_for_codepoints(6) == 2, "six-letter ORP index");
    require(orp_index_for_codepoints(9) == 2, "nine-letter ORP index");
    require(orp_index_for_codepoints(10) == 3, "ten-letter ORP index");
    require(orp_index_for_codepoints(13) == 3, "thirteen-letter ORP index");
    require(orp_index_for_codepoints(14) == 4, "fourteen-letter ORP index");
    require(centered_index_for_codepoints(10) == 4, "centered fallback even length");
    require(centered_index_for_codepoints(11) == 5, "centered fallback odd length");
}

void test_display_frames_without_chunking() {
    const auto frames = make_display_frames(tokenize("a rapid testing"));
    require(frames.size() == 3, "display frame count without chunking");
    require(frames[0].text_utf8 == "a", "single-letter frame text");
    require(frames[0].orp_codepoint_index == 0, "single-letter frame ORP");
    require(frames[1].text_utf8 == "rapid", "rapid frame text");
    require(frames[1].orp_codepoint_index == 1, "rapid frame ORP");
    require(!frames[1].centered_fallback, "normal frame not centered fallback");
    require(frames[2].text_utf8 == "testing", "testing frame text");
    require(frames[2].orp_codepoint_index == 2, "testing frame ORP");
}

void test_display_frames_chunk_long_tokens() {
    DisplayFrameOptions options;
    options.max_codepoints_per_frame = 8;

    const auto frames = make_display_frames(tokenize("supercalifragilistic!"), options);
    require(frames.size() == 3, "long token chunk count");
    require(frames[0].text_utf8 == "supercal-", "first chunk has continuation marker");
    require(frames[0].source_byte_start == 0, "first chunk source byte start");
    require(frames[0].source_byte_length == 8, "first chunk source byte length");
    require(frames[0].chunk_index == 0, "first chunk index");
    require(frames[0].chunk_count == 3, "first chunk count");
    require(frames[0].continuation_after, "first chunk continues after");
    require(!frames[0].continuation_before, "first chunk no continuation before");
    require(frames[0].centered_fallback, "first chunk uses centered fallback");
    require(frames[0].orp_codepoint_index == 4, "first chunk centered ORP index");
    require(!has_flag(frames[0].punctuation_flags, PunctuationFlags::sentence_end),
            "first chunk does not carry sentence punctuation");

    require(frames[1].text_utf8 == "ifragili-", "second chunk text");
    require(frames[1].source_byte_start == 8, "second chunk source byte start");
    require(frames[1].source_byte_length == 8, "second chunk source byte length");
    require(frames[1].continuation_before, "second chunk continuation before");
    require(frames[1].continuation_after, "second chunk continuation after");

    require(frames[2].text_utf8 == "stic!", "final chunk text");
    require(frames[2].source_byte_start == 16, "final chunk source byte start");
    require(frames[2].source_byte_length == 5, "final chunk source byte length");
    require(frames[2].continuation_before, "final chunk continuation before");
    require(!frames[2].continuation_after, "final chunk does not continue after");
    require(has_flag(frames[2].punctuation_flags, PunctuationFlags::sentence_end),
            "final chunk carries sentence punctuation");
}

void test_display_frames_preserve_utf8_boundaries() {
    DisplayFrameOptions options;
    options.max_codepoints_per_frame = 3;
    const auto frames = make_display_frames(tokenize("éclair"), options);

    require(frames.size() == 2, "utf8 chunk count");
    require(frames[0].text_utf8 == "\xC3\xA9" "cl-", "utf8 first chunk text");
    require(frames[0].source_byte_start == 0, "utf8 first chunk source start");
    require(frames[0].source_byte_length == 4, "utf8 first chunk source length");
    require(utf8_codepoint_count(frames[0].text_utf8) == 4,
            "utf8 first chunk display codepoint count includes marker");
    require(frames[1].text_utf8 == "air", "utf8 final chunk text");
    require(frames[1].source_byte_start == 4, "utf8 final chunk source start");
    require(frames[1].source_byte_length == 3, "utf8 final chunk source length");
}

void test_chapters_from_markdown_headings() {
    const std::string source = "# One\nAlpha beta.\n# Two\nGamma delta.";
    const auto frames = make_display_frames(tokenize(source));
    const auto chapters = make_chapters_from_markdown_headings(source, frames);

    require(chapters.size() == 2, "markdown chapter count");
    require(chapters[0].title == "One", "first markdown chapter title");
    require(chapters[1].title == "Two", "second markdown chapter title");
    require(chapters[0].first_frame_index == 0, "first chapter starts at first frame");
    require(chapters[0].frame_count > 0, "first chapter has frames");
    require(chapters[1].first_frame_index > chapters[0].first_frame_index,
            "second chapter starts after first");
    require(chapters[1].frame_count > 0, "second chapter has frames");

    const auto fallback = make_chapters_from_markdown_headings(
        "No heading here.", make_display_frames(tokenize("No heading here.")),
        "Fallback");
    require(fallback.size() == 1, "fallback chapter count");
    require(fallback[0].title == "Fallback", "fallback chapter title");
}

void test_chapters_from_gutenberg_roman_headings() {
    const std::string source =
        "Contents\n"
        "   I.     A Scandal in Bohemia\n\n"
        "I. A SCANDAL IN BOHEMIA\n\n"
        "Alpha beta.\n\n"
        "I.\n\n"
        "Inside sentence.\n\n"
        "II. THE RED-HEADED LEAGUE\n\n"
        "Gamma delta.";
    const auto frames = make_display_frames(tokenize(source));
    const auto chapters = make_chapters_from_markdown_headings(source, frames);

    require(chapters.size() == 2, "gutenberg chapter count");
    require(chapters[0].title == "I. A SCANDAL IN BOHEMIA",
            "first gutenberg chapter title");
    require(chapters[1].title == "II. THE RED-HEADED LEAGUE",
            "second gutenberg chapter title");
    require(chapters[0].first_frame_index > 0,
            "gutenberg contents preface is not a chapter");
    require(chapters[1].first_frame_index > chapters[0].first_frame_index,
            "second gutenberg chapter starts later");
}

void test_txt_import_extracts_metadata_and_headings() {
    const std::string source =
        "Title: Example Book\n"
        "Author: Example Writer\n\n"
        "# Chapter One\n"
        "Alpha beta.\n\n"
        "# Chapter Two\n"
        "Gamma delta.";

    const auto document =
        import_txt_document(source, "/books/Fallback Writer - Fallback Title.txt");
    require(document.format == DocumentFormat::txt, "txt import format");
    require(document.title == "Example Book", "txt import explicit title");
    require(document.author == "Example Writer", "txt import explicit author");
    require(document.metadata_confidence == MetadataConfidence::explicit_metadata,
            "txt import explicit metadata confidence");
    require(document.chapter_source == ChapterSource::explicit_headings,
            "txt import explicit chapter source");
    require(document.chapters.size() == 2, "txt import heading chapter count");
    require(document.chapters[0].title == "Chapter One", "txt import first heading title");
    require(document.chapters[1].title == "Chapter Two", "txt import second heading title");
    require(document.normalized_text_utf8 == source, "txt import preserves text");

    const auto reading_tokens = tokenize(document.reading_text_utf8);
    require(!reading_tokens.empty(), "txt import reading text has body tokens");
    require(reading_tokens.front().text_utf8 == "Alpha",
            "txt import reading text skips metadata and heading");
}

void test_txt_import_uses_filename_fallback() {
    TxtImportOptions options;
    options.target_chunk_words = 100;
    const auto document =
        import_txt_document("Plain text without headings.", "/books/Arthur - Sample.txt",
                            options);

    require(document.title == "Sample", "txt import filename title");
    require(document.author == "Arthur", "txt import filename author");
    require(document.metadata_confidence == MetadataConfidence::filename_convention,
            "txt import filename confidence");
    require(document.chapter_source == ChapterSource::single_fallback,
            "txt import single fallback chapter source");
    require(document.chapters.size() == 1, "txt import single fallback chapter count");
    require(document.chapters[0].title == "Sample", "txt import fallback chapter title");
}

void test_txt_import_auto_chunks_long_plain_text() {
    TxtImportOptions options;
    options.target_chunk_words = 4;
    const std::string source =
        "one two three four\n"
        "five six seven eight\n"
        "nine ten eleven twelve";

    const auto document = import_txt_document(source, "Long Plain.txt", options);
    require(document.chapter_source == ChapterSource::auto_chunked,
            "txt import auto chunk chapter source");
    require(document.chapters.size() == 3, "txt import auto chunk count");
    require(document.chapters[0].title == "Part 1", "txt import first chunk title");
    require(document.chapters[1].source_byte_start ==
                document.chapters[0].source_byte_start +
                    document.chapters[0].source_byte_length,
            "txt import chunks are contiguous");
    require(document.chapters[2].source_byte_start +
                document.chapters[2].source_byte_length ==
                source.size(),
            "txt import final chunk reaches end");
}

void test_document_text_frame_chapter_mapping() {
    const std::string source = "# One\nAlpha beta.\n# Two\nGamma delta.";
    const auto document = import_txt_document(source);
    const auto frames = make_display_frames(tokenize(document.reading_text_utf8));
    const auto chapters = make_frame_chapters(document, frames);

    require(chapters.size() == 2, "document text frame chapter count");
    require(chapters[0].title == "One", "document text first frame chapter title");
    require(chapters[1].title == "Two", "document text second frame chapter title");
    require(frames[chapters[0].first_frame_index].text_utf8 == "Alpha",
            "document text first chapter starts at body text");
    require(frames[chapters[1].first_frame_index].text_utf8 == "Gamma",
            "document text second chapter starts at body text");
    require(chapters[1].first_frame_index > chapters[0].first_frame_index,
            "document text second frame chapter starts later");
}

void test_txt_import_hides_skipped_front_matter() {
    const std::string source =
        "Project boilerplate that should not be read.\n\n"
        "I. FIRST STORY\n\n"
        "Alpha beta.\n\n"
        "II. SECOND STORY\n\n"
        "Gamma delta.";
    const auto document = import_txt_document(source, "Stories.txt");
    const auto frames = make_display_frames(tokenize(document.reading_text_utf8));
    const auto chapters = make_frame_chapters(document, frames);

    require(document.chapter_source == ChapterSource::explicit_headings,
            "txt import front matter fixture has explicit chapters");
    require(!frames.empty(), "txt import front matter fixture has frames");
    require(frames.front().text_utf8 == "Alpha",
            "txt import hides skipped front matter and first heading");
    require(chapters.size() == 2, "txt import front matter chapter count");
    require(chapters[0].first_frame_index == 0,
            "txt import first chapter starts at first readable frame");
}

void test_playback_advances_by_display_frame() {
    DisplayFrameOptions options;
    options.max_codepoints_per_frame = 8;
    auto frames = make_display_frames(tokenize("supercalifragilistic! done"), options);
    PlaybackEngine engine(frames, PlaybackSettings{.wpm = 300});

    require(engine.snapshot().frame_count == 4, "playback frame count includes chunks");
    require(engine.snapshot().token_count == 4, "legacy token count mirrors frame count");
    require(engine.snapshot().visible_frame.has_value(), "initial visible frame exists");
    require(engine.snapshot().visible_frame->text_utf8 == "supercal-",
            "initial visible frame text");
    require(engine.snapshot().visible_token->text_utf8 == "supercal-",
            "legacy visible token mirrors frame text");
    require(engine.snapshot().source_token_index == 0, "initial source token index");
    require(engine.snapshot().chunk_index == 0, "initial chunk index");
    require(engine.snapshot().chunk_count == 3, "initial chunk count");
    require(engine.snapshot().visible_frame->centered_fallback,
            "chunked frame uses centered fallback");

    engine.start(0);
    require(engine.tick(200), "first frame tick advances");
    require(engine.snapshot().frame_index == 1, "playback frame index advances");
    require(engine.snapshot().visible_frame->text_utf8 == "ifragili-",
            "second chunk visible");
    require(engine.snapshot().source_token_index == 0, "second chunk source token index");
    require(engine.snapshot().chunk_index == 1, "second chunk index");

    require(engine.tick(400), "second frame tick advances");
    require(engine.snapshot().visible_frame->text_utf8 == "stic!",
            "final chunk visible");
    require(has_flag(engine.snapshot().visible_frame->punctuation_flags,
                     PunctuationFlags::sentence_end),
            "final chunk carries punctuation in playback");

    require(engine.duration_for_index(2) == 400, "final sentence chunk duration");
    require(engine.duration_for_index(3) == 200, "plain next token duration");

    engine.advance_one(400);
    require(engine.snapshot().frame_index == 3, "manual advance reaches next token frame");
    require(engine.snapshot().source_token_index == 1, "next token source index");
    require(engine.snapshot().visible_frame->text_utf8 == "done", "next token text");
    engine.rewind_one(400);
    require(engine.snapshot().frame_index == 2, "rewind returns to final chunk");
    require(engine.snapshot().chunk_index == 2, "rewind chunk index");
}

void test_playback_exposes_chapter_progress_and_remaining_time() {
    const auto frames = make_display_frames(tokenize("one two. three four."));
    std::vector<Chapter> chapters;
    Chapter first;
    first.title = "First";
    first.source_byte_start = 0;
    first.source_byte_length = 8;
    first.first_frame_index = 0;
    first.frame_count = 2;
    chapters.push_back(first);

    Chapter second;
    second.title = "Second";
    second.source_byte_start = 9;
    second.source_byte_length = 11;
    second.first_frame_index = 2;
    second.frame_count = 2;
    chapters.push_back(second);

    PlaybackEngine engine(frames, chapters, PlaybackSettings{.wpm = 300});
    require(engine.snapshot().chapter_count == 2, "playback chapter count");
    require(engine.snapshot().chapter_index == 0, "initial chapter index");
    require(engine.snapshot().chapter_title == "First", "initial chapter title");
    require(engine.snapshot().chapter_frame_index == 0,
            "initial chapter frame index");
    require(engine.snapshot().chapter_frame_count == 2,
            "initial chapter frame count");
    require(engine.snapshot().estimated_chapter_remaining_ms == 600,
            "initial chapter remaining includes sentence pause");
    require(engine.snapshot().estimated_remaining_ms == 1200,
            "initial document remaining includes both chapters");

    engine.seek(2, 0);
    require(engine.snapshot().chapter_index == 1, "seek reaches second chapter");
    require(engine.snapshot().chapter_title == "Second", "second chapter title");
    require(engine.snapshot().chapter_frame_index == 0,
            "second chapter local frame index");
    require(engine.snapshot().estimated_chapter_remaining_ms == 600,
            "second chapter remaining");
    require(engine.snapshot().estimated_remaining_ms == 600,
            "document remaining from second chapter");
}

void test_playback_word_navigation_pauses_and_skips_chunks() {
    DisplayFrameOptions options;
    options.max_codepoints_per_frame = 8;
    auto frames = make_display_frames(tokenize("one supercalifragilistic! three"), options);
    PlaybackEngine engine(frames, PlaybackSettings{.wpm = 300});

    engine.start(0);
    require(engine.snapshot().state == PlaybackState::playing,
            "word navigation fixture starts playing");

    engine.advance_word(0);
    require(engine.snapshot().state == PlaybackState::paused,
            "advance word pauses playback");
    require(engine.snapshot().source_token_index == 1,
            "advance word reaches next source token");
    require(engine.snapshot().chunk_index == 0,
            "advance word lands on first chunk");
    require(engine.snapshot().visible_frame->text_utf8 == "supercal-",
            "advance word skips to long word first chunk");

    engine.advance_one(0);
    require(engine.snapshot().chunk_index == 1, "fixture moves inside long word");
    engine.advance_word(0);
    require(engine.snapshot().state == PlaybackState::paused,
            "advance from chunk pauses playback");
    require(engine.snapshot().source_token_index == 2,
            "advance word skips remaining chunks");
    require(engine.snapshot().visible_frame->text_utf8 == "three",
            "advance word lands on following word");

    engine.rewind_word(0);
    require(engine.snapshot().state == PlaybackState::paused,
            "rewind word pauses playback");
    require(engine.snapshot().source_token_index == 1,
            "rewind word reaches previous source token");
    require(engine.snapshot().chunk_index == 0,
            "rewind word lands on first chunk of previous word");
}

void test_playback_sentence_rewind_pauses_and_repeats() {
    auto frames = make_display_frames(tokenize("First one. Second two. Third three."));
    PlaybackEngine engine(frames, PlaybackSettings{.wpm = 300});

    engine.seek(5, 0);
    engine.start(0);
    require(engine.snapshot().visible_frame->text_utf8 == "three.",
            "sentence fixture starts inside third sentence");

    engine.rewind_sentence(0);
    require(engine.snapshot().state == PlaybackState::paused,
            "sentence rewind pauses playback");
    require(engine.snapshot().visible_frame->text_utf8 == "Third",
            "sentence rewind inside sentence lands at current sentence start");

    engine.rewind_sentence(0);
    require(engine.snapshot().visible_frame->text_utf8 == "Second",
            "second sentence rewind lands at previous sentence start");

    engine.rewind_sentence(0);
    require(engine.snapshot().visible_frame->text_utf8 == "First",
            "third sentence rewind lands at first sentence start");

    engine.rewind_sentence(0);
    require(engine.snapshot().visible_frame->text_utf8 == "First",
            "sentence rewind clamps at document start");
}

void test_playback_sentence_context_window() {
    auto frames = make_display_frames(tokenize("First one. Second two. Third three."));
    PlaybackEngine engine(frames, PlaybackSettings{.wpm = 300});
    engine.seek(3, 0);

    const auto context = engine.current_sentence_context();
    require(context.current_frame_index == 3, "context current frame index");
    require(context.current_source_token_index == frames[3].source_token_index,
            "context current source token index");
    require(context.start_frame_index == 0,
            "context includes previous sentence start");
    require(context.end_frame_index == 5,
            "context includes next sentence end");

    const auto first_context = engine.sentence_context_for_frame(0);
    require(first_context.start_frame_index == 0,
            "first context clamps start");
    require(first_context.end_frame_index == 3,
            "first context includes next sentence");
}

void test_playback_exports_frame_navigation_targets() {
    auto frames = make_display_frames(tokenize("First one. Second two. Third three."));
    PlaybackEngine engine(frames, PlaybackSettings{.wpm = 300});

    const auto middle = engine.navigation_for_frame(3);
    require(middle.previous_word_frame_index == 2,
            "navigation previous word target");
    require(middle.next_word_frame_index == 4,
            "navigation next word target");
    require(middle.sentence_rewind_frame_index == 2,
            "navigation sentence rewind target");
    require(!middle.starts_chapter, "navigation middle frame not chapter start");

    const auto first = engine.navigation_for_frame(0);
    require(first.previous_word_frame_index == 0,
            "navigation first previous clamps");
    require(first.sentence_rewind_frame_index == 0,
            "navigation first sentence rewind clamps");
    require(first.starts_chapter, "navigation first frame starts fallback chapter");
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
    bookmark.frame_index = 8;
    bookmark.source_token_index = 5;
    bookmark.chunk_index = 2;
    bookmark.updated_at = 123456;

    const auto parsed = parse_bookmark(serialize_bookmark(bookmark));
    require(parsed.has_value(), "bookmark round trip parses");
    require(parsed->doc_id == "bundled-sample", "bookmark doc id round trip");
    require(parsed->content_hash == hash, "bookmark hash round trip");
    require(parsed->source_byte_offset == 42, "bookmark source offset round trip");
    require(parsed->token_index == 7, "bookmark token index round trip");
    require(parsed->frame_index == 8, "bookmark frame index round trip");
    require(parsed->source_token_index == 5, "bookmark source token round trip");
    require(parsed->chunk_index == 2, "bookmark chunk index round trip");
    require(parsed->updated_at == 123456, "bookmark updated time round trip");

    const auto dir = temp_test_dir("bookmark");
    const auto path = dir / "bookmark.state";
    require(save_bookmark_atomic(path, bookmark), "bookmark atomic save succeeds");
    const auto loaded = load_bookmark(path);
    require(loaded.has_value(), "bookmark atomic load succeeds");
    require(loaded->token_index == 7, "loaded bookmark token index");
    require(loaded->frame_index == 8, "loaded bookmark frame index");
    require(!std::filesystem::exists(path.string() + ".tmp"), "bookmark temp file removed");

    const auto legacy = parse_bookmark(
        "schema_version=1\n"
        "doc_id=bundled-sample\n"
        "content_hash=0123456789abcdef\n"
        "source_byte_offset=10\n"
        "token_index=3\n"
        "updated_at=99\n");
    require(legacy.has_value(), "legacy bookmark without frame fields parses");
    require(legacy->frame_index == 3, "legacy bookmark frame falls back to token");
    require(legacy->source_token_index == 3, "legacy bookmark source token falls back");
    require(legacy->chunk_index == 0, "legacy bookmark chunk defaults to first chunk");

    {
        std::ofstream corrupt(path, std::ios::binary | std::ios::trunc);
        corrupt << "schema_version=1\ncontent_hash=" << to_hex(hash) << '\n';
    }
    require(!load_bookmark(path).has_value(), "corrupt bookmark is rejected");

    std::error_code error;
    std::filesystem::remove_all(dir, error);
}

void test_lifecycle_sleep_pauses_persists_and_does_not_advance() {
    const std::string source = "one two three";
    const auto frames = make_display_frames(tokenize(source));
    const DocumentIdentity document{"sleep-fixture", content_hash(source)};
    const auto dir = temp_test_dir("lifecycle-sleep");
    const ReaderStatePaths paths{dir / "settings.state", dir / "bookmark.state"};

    DisplayAdapter adapter(
        frames, PlaybackSettings{.wpm = 300},
        [](const DisplayState&) {});
    adapter.start(0);
    adapter.tick(200);
    require(adapter.snapshot().frame_index == 1, "sleep fixture advances to second frame");
    require(adapter.snapshot().state == PlaybackState::playing,
            "sleep fixture is playing before sleep");

    require(pause_and_persist_for_sleep(adapter, paths, document, 1234),
            "sleep lifecycle persists state");
    require(adapter.snapshot().state == PlaybackState::paused,
            "sleep lifecycle pauses playback");
    require(adapter.snapshot().frame_index == 1,
            "sleep lifecycle keeps current frame");
    require(!adapter.tick(100000),
            "sleep lifecycle prevents monotonic catch-up advancement");
    require(adapter.snapshot().frame_index == 1,
            "sleep lifecycle remains on same frame after later tick");

    const auto loaded_settings = load_settings(paths.settings_path);
    const auto loaded_bookmark = load_bookmark(paths.bookmark_path);
    require(loaded_settings.has_value(), "sleep lifecycle saved settings");
    require(loaded_settings->wpm == 300, "sleep lifecycle saved wpm");
    require(loaded_bookmark.has_value(), "sleep lifecycle saved bookmark");
    require(bookmark_matches_document(*loaded_bookmark, document),
            "sleep lifecycle bookmark matches document");
    require(loaded_bookmark->frame_index == 1, "sleep lifecycle saved frame index");
    require(loaded_bookmark->updated_at == 1234, "sleep lifecycle saved timestamp");

    DisplayAdapter restored(
        frames, to_playback_settings(*loaded_settings),
        [](const DisplayState&) {});
    restored.seek(resolve_bookmark_frame(*loaded_bookmark, frames), 5000);
    require(restored.snapshot().frame_index == 1, "sleep lifecycle restore frame");
    require(restored.snapshot().visible_frame->text_utf8 == "two",
            "sleep lifecycle restore visible frame");

    std::error_code error;
    std::filesystem::remove_all(dir, error);
}

void test_lifecycle_bookmark_resolution_falls_back_to_source_identity() {
    DisplayFrameOptions options;
    options.max_codepoints_per_frame = 8;
    const auto frames = make_display_frames(tokenize("one supercalifragilistic! three"),
                                            options);
    const DocumentIdentity document{"doc", 42};

    DisplayAdapter adapter(
        frames, PlaybackSettings{},
        [](const DisplayState&) {});
    adapter.seek(1, 0);
    const auto captured = capture_reader_state(document, adapter.snapshot(),
                                               adapter.settings(), 99);

    auto moved = captured.bookmark;
    moved.frame_index = 99;
    require(resolve_bookmark_frame(moved, frames) == 1,
            "bookmark resolves by source token and chunk when frame changed");

    auto offset_only = captured.bookmark;
    offset_only.frame_index = 99;
    offset_only.source_token_index = 99;
    offset_only.chunk_index = 99;
    require(resolve_bookmark_frame(offset_only, frames) == 1,
            "bookmark resolves by source byte offset as final fallback");
}

void test_controller_lifecycle_sleep_persists_and_restores_paused() {
    const std::string source = "one two three four";
    const auto frames = make_display_frames(tokenize(source));
    const DocumentIdentity document{"controller-sleep-fixture", content_hash(source)};
    const auto dir = temp_test_dir("controller-sleep");
    const ReaderStatePaths paths{dir / "settings.state", dir / "bookmark.state"};

    ReaderController controller(frames, {}, PlaybackSettings{.wpm = 300});
    controller.handle_input(ReaderInput::dpad_up, 0);
    controller.handle_input(ReaderInput::red, 10);
    require(controller.tick(210), "controller lifecycle advances before sleep");
    require(controller.playback().snapshot().frame_index == 1,
            "controller lifecycle reaches second frame");
    require(controller.playback().snapshot().wpm == 325,
            "controller lifecycle changed WPM before sleep");

    require(pause_and_persist_for_sleep(controller, paths, document, 9000),
            "controller lifecycle sleep persists");
    require(controller.view().mode == ReaderDisplayMode::sleep,
            "controller lifecycle enters sleep mode");
    require(controller.playback().snapshot().state == PlaybackState::paused,
            "controller lifecycle sleep pauses playback");
    require(!controller.tick(20000),
            "controller lifecycle sleeping tick does not advance");

    auto restored = restore_reader_controller(frames, {}, paths, document, {}, 30000);
    require(restored.view().mode == ReaderDisplayMode::title,
            "controller lifecycle restore wakes to title mode");
    require(restored.playback().snapshot().state == PlaybackState::paused,
            "controller lifecycle restore is paused");
    require(restored.playback().snapshot().frame_index == 1,
            "controller lifecycle restore keeps frame");
    require(restored.playback().snapshot().visible_frame->text_utf8 == "two",
            "controller lifecycle restore keeps visible word");
    require(restored.playback().snapshot().wpm == 325,
            "controller lifecycle restore keeps WPM");
    require(!restored.tick(50000),
            "controller lifecycle restore does not catch up old time");

    restored.handle_input(ReaderInput::red, 50000);
    require(restored.playback().snapshot().state == PlaybackState::playing,
            "controller lifecycle restored red resumes");
    require(!restored.tick(50184),
            "controller lifecycle restored resume schedules from red press");
    require(restored.tick(50185),
            "controller lifecycle restored playback advances on new schedule");
    require(restored.playback().snapshot().frame_index == 2,
            "controller lifecycle restored playback advances to next frame");

    std::error_code error;
    std::filesystem::remove_all(dir, error);
}

void test_controller_restore_ignores_bookmark_for_different_document() {
    const std::string source = "one two three";
    const auto frames = make_display_frames(tokenize(source));
    const DocumentIdentity saved_document{"saved-doc", content_hash(source)};
    const DocumentIdentity other_document{"other-doc", content_hash(source)};
    const auto dir = temp_test_dir("controller-restore-mismatch");
    const ReaderStatePaths paths{dir / "settings.state", dir / "bookmark.state"};

    ReaderController controller(frames, {}, PlaybackSettings{.wpm = 250});
    controller.handle_input(ReaderInput::red, 0);
    controller.tick(240);
    require(pause_and_persist_for_sleep(controller, paths, saved_document, 77),
            "controller mismatch fixture persists");

    auto restored = restore_reader_controller(frames, {}, paths, other_document, {}, 1000);
    require(restored.view().mode == ReaderDisplayMode::title,
            "controller mismatch restore starts on title");
    require(restored.playback().snapshot().state == PlaybackState::idle,
            "controller mismatch restore stays idle without matching bookmark");
    require(restored.playback().snapshot().frame_index == 0,
            "controller mismatch restore ignores old bookmark frame");

    std::error_code error;
    std::filesystem::remove_all(dir, error);
}

void test_reader_controller_red_sleep_wake_and_pause_sequence() {
    ReaderController controller(make_display_frames(tokenize("one two three")),
                                {},
                                PlaybackSettings{.wpm = 300});

    require(controller.view().mode == ReaderDisplayMode::title,
            "controller starts on title card");
    require(controller.playback().snapshot().state == PlaybackState::idle,
            "controller playback starts idle");

    require(controller.handle_input(ReaderInput::red, 0),
            "red leaves title and starts playback");
    require(controller.view().mode == ReaderDisplayMode::rsvp,
            "red enters RSVP mode");
    require(controller.playback().snapshot().state == PlaybackState::playing,
            "red starts playback");

    require(controller.tick(200), "controller playback advances before sleep");
    require(controller.playback().snapshot().frame_index == 1,
            "controller advanced to second word");

    require(controller.handle_input(ReaderInput::red, 210),
            "red pauses while awake");
    require(controller.playback().snapshot().state == PlaybackState::paused,
            "controller pauses while awake");

    require(controller.handle_input(ReaderInput::sleep, 300),
            "sleep input enters sleep mode");
    require(controller.view().mode == ReaderDisplayMode::sleep,
            "controller shows sleep mode");
    require(!controller.tick(5000), "sleep mode does not advance");
    require(controller.playback().snapshot().frame_index == 1,
            "sleep keeps the current frame");

    require(controller.handle_input(ReaderInput::red, 6000),
            "red wakes from sleep");
    require(controller.view().mode == ReaderDisplayMode::title,
            "red wake returns to title card");
    require(controller.playback().snapshot().state == PlaybackState::paused,
            "red wake does not resume playback");

    require(controller.handle_input(ReaderInput::red, 6100),
            "second red resumes from title card");
    require(controller.view().mode == ReaderDisplayMode::rsvp,
            "second red returns to RSVP mode");
    require(controller.playback().snapshot().state == PlaybackState::playing,
            "second red resumes playback");
    require(!controller.tick(6299), "resume is scheduled from wake interaction");
    require(controller.playback().snapshot().frame_index == 1,
            "resume does not catch up skipped sleep time");

    require(controller.handle_input(ReaderInput::red, 6300),
            "third red pauses after wake/resume");
    require(controller.playback().snapshot().state == PlaybackState::paused,
            "red remains usable for pause after wake/resume");
}

void test_reader_controller_dpad_navigation_and_context_mode() {
    ReaderController controller(make_display_frames(tokenize("one two three four")),
                                {},
                                PlaybackSettings{.wpm = 250});

    controller.handle_input(ReaderInput::red, 0);
    controller.handle_input(ReaderInput::dpad_right, 10);
    require(controller.playback().snapshot().state == PlaybackState::paused,
            "dpad right pauses playback");
    require(controller.playback().snapshot().frame_index == 1,
            "dpad right advances one word");
    require(controller.view().mode == ReaderDisplayMode::rsvp,
            "short dpad right keeps RSVP mode");

    controller.handle_input(ReaderInput::hold_right, 20);
    require(controller.view().mode == ReaderDisplayMode::context,
            "holding dpad enters context mode");
    require(controller.playback().snapshot().frame_index == 2,
            "holding dpad steps while entering context");

    controller.handle_input(ReaderInput::hold_end, 30);
    require(controller.view().mode == ReaderDisplayMode::rsvp,
            "releasing dpad exits context mode");
    require(controller.playback().snapshot().state == PlaybackState::paused,
            "context release keeps playback paused");

    controller.handle_input(ReaderInput::dpad_up, 40);
    require(controller.playback().snapshot().wpm == 275,
            "dpad up increases WPM by 25");
    controller.handle_input(ReaderInput::dpad_down, 50);
    require(controller.playback().snapshot().wpm == 250,
            "dpad down decreases WPM by 25");
}

void test_platform_control_event_mapping() {
    require(!reader_input_for_control_event(
                ControlEvent{PhysicalControl::red_button, ControlPhase::press, 0})
                .has_value(),
            "platform ignores red press until release");
    require(reader_input_for_control_event(
                ControlEvent{PhysicalControl::red_button, ControlPhase::release, 10})
                .value() == ReaderInput::red,
            "platform maps red release to red input");
    require(reader_input_for_control_event(
                ControlEvent{PhysicalControl::touch_primary, ControlPhase::release, 10})
                .value() == ReaderInput::red,
            "platform maps primary touch release to red input");
    require(reader_input_for_control_event(
                ControlEvent{PhysicalControl::blue_button, ControlPhase::release, 10})
                .value() == ReaderInput::blue,
            "platform maps blue release to menu input");
    require(reader_input_for_control_event(
                ControlEvent{PhysicalControl::dpad_left, ControlPhase::release, 10})
                .value() == ReaderInput::dpad_left,
            "platform maps dpad left release");
    require(reader_input_for_control_event(
                ControlEvent{PhysicalControl::dpad_right, ControlPhase::release, 10})
                .value() == ReaderInput::dpad_right,
            "platform maps dpad right release");
    require(reader_input_for_control_event(
                ControlEvent{PhysicalControl::dpad_up, ControlPhase::release, 10})
                .value() == ReaderInput::dpad_up,
            "platform maps dpad up release");
    require(reader_input_for_control_event(
                ControlEvent{PhysicalControl::dpad_down, ControlPhase::release, 10})
                .value() == ReaderInput::dpad_down,
            "platform maps dpad down release");
}

void test_platform_hold_and_lifecycle_mapping() {
    require(reader_input_for_control_event(
                ControlEvent{PhysicalControl::dpad_left, ControlPhase::hold_start, 100})
                .value() == ReaderInput::hold_left,
            "platform maps dpad left hold start");
    require(reader_input_for_control_event(
                ControlEvent{PhysicalControl::dpad_left, ControlPhase::hold_repeat, 500})
                .value() == ReaderInput::hold_left,
            "platform maps dpad left hold repeat");
    require(reader_input_for_control_event(
                ControlEvent{PhysicalControl::dpad_right, ControlPhase::hold_start, 100})
                .value() == ReaderInput::hold_right,
            "platform maps dpad right hold start");
    require(reader_input_for_control_event(
                ControlEvent{PhysicalControl::dpad_right, ControlPhase::hold_end, 600})
                .value() == ReaderInput::hold_end,
            "platform maps dpad hold end");
    require(!reader_input_for_control_event(
                ControlEvent{PhysicalControl::red_button, ControlPhase::hold_start, 100})
                .has_value(),
            "platform ignores red hold start");

    require(reader_input_for_lifecycle_event(
                LifecycleEvent{LifecycleSignal::sleep, 1000})
                .value() == ReaderInput::sleep,
            "platform maps sleep lifecycle");
    require(reader_input_for_lifecycle_event(
                LifecycleEvent{LifecycleSignal::suspend, 1000})
                .value() == ReaderInput::sleep,
            "platform maps suspend lifecycle");
    require(reader_input_for_lifecycle_event(
                LifecycleEvent{LifecycleSignal::exit, 1000})
                .value() == ReaderInput::sleep,
            "platform maps exit lifecycle");
    require(reader_input_for_lifecycle_event(
                LifecycleEvent{LifecycleSignal::wake, 1000})
                .value() == ReaderInput::red,
            "platform maps wake lifecycle");
    require(!reader_input_for_lifecycle_event(
                LifecycleEvent{LifecycleSignal::resume, 1000})
                .has_value(),
            "platform resume does not imply playback");
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
    test_display_frame_orp_indexes();
    test_display_frames_without_chunking();
    test_display_frames_chunk_long_tokens();
    test_display_frames_preserve_utf8_boundaries();
    test_chapters_from_markdown_headings();
    test_chapters_from_gutenberg_roman_headings();
    test_txt_import_extracts_metadata_and_headings();
    test_txt_import_uses_filename_fallback();
    test_txt_import_auto_chunks_long_plain_text();
    test_document_text_frame_chapter_mapping();
    test_txt_import_hides_skipped_front_matter();
    test_playback_advances_by_display_frame();
    test_playback_exposes_chapter_progress_and_remaining_time();
    test_playback_word_navigation_pauses_and_skips_chunks();
    test_playback_sentence_rewind_pauses_and_repeats();
    test_playback_sentence_context_window();
    test_playback_exports_frame_navigation_targets();
    test_settings_serialization_and_atomic_load();
    test_bookmark_serialization_hash_and_atomic_load();
    test_lifecycle_sleep_pauses_persists_and_does_not_advance();
    test_lifecycle_bookmark_resolution_falls_back_to_source_identity();
    test_controller_lifecycle_sleep_persists_and_restores_paused();
    test_controller_restore_ignores_bookmark_for_different_document();
    test_reader_controller_red_sleep_wake_and_pause_sequence();
    test_reader_controller_dpad_navigation_and_context_mode();
    test_platform_control_event_mapping();
    test_platform_hold_and_lifecycle_mapping();

    if (failures != 0) {
        std::cerr << failures << " test failure(s)\n";
        return EXIT_FAILURE;
    }
    std::cout << "All RSVP core tests passed\n";
    return EXIT_SUCCESS;
}

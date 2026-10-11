#include "rsvp/ui/reader_view_model.hpp"

#include <algorithm>
#include <string>

namespace rsvp::ui {
namespace {

ReaderVisualMode visual_mode(rsvp::controller::ReaderDisplayMode mode) {
    switch (mode) {
    case rsvp::controller::ReaderDisplayMode::title:
        return ReaderVisualMode::title;
    case rsvp::controller::ReaderDisplayMode::rsvp:
        return ReaderVisualMode::rsvp;
    case rsvp::controller::ReaderDisplayMode::context:
        return ReaderVisualMode::context;
    case rsvp::controller::ReaderDisplayMode::sleep:
        return ReaderVisualMode::sleep;
    }
    return ReaderVisualMode::rsvp;
}

std::uint64_t ceil_ms_to_seconds(std::uint64_t ms) {
    return (ms + 999U) / 1000U;
}

std::string context_chunk_text(const rsvp::text::DisplayFrame& frame) {
    auto text = frame.text_utf8;
    if (frame.continuation_after && !text.empty() && text.back() == '-') {
        text.pop_back();
    }
    return text;
}

} // namespace

ReaderViewModel make_reader_view_model(const rsvp::controller::ReaderView& view) {
    const auto& state = view.playback;

    ReaderViewModel model;
    model.mode = visual_mode(view.mode);
    model.chapter_title = state.chapter_title;
    model.chapter_index = state.chapter_count == 0 ? std::size_t{0} : state.chapter_index + 1;
    model.chapter_count = state.chapter_count;
    model.chapter_position =
        state.chapter_frame_count == 0 ? std::size_t{0} : state.chapter_frame_index + 1;
    model.chapter_total = state.chapter_frame_count;
    model.frame_position = state.frame_count == 0 ? std::size_t{0} : state.frame_index + 1;
    model.frame_total = state.frame_count;
    model.wpm = state.wpm;
    if (state.visible_frame) {
        model.orp_codepoint_index = state.visible_frame->orp_codepoint_index;
        model.font_scale_percent = state.visible_frame->font_scale_percent;
    }
    model.remaining_seconds = ceil_ms_to_seconds(state.estimated_remaining_ms);
    model.chapter_remaining_seconds = ceil_ms_to_seconds(state.estimated_chapter_remaining_ms);
    model.playing = state.state == rsvp::playback::PlaybackState::playing;
    model.paused = state.state == rsvp::playback::PlaybackState::paused;
    model.finished = state.state == rsvp::playback::PlaybackState::finished;

    if (state.chapter_frame_count <= 1) {
        model.progress_percent = state.chapter_frame_count == 0 ? 0 : 100;
    } else {
        model.progress_percent = static_cast<int>(
            (state.chapter_frame_index * std::size_t{100}) /
            (state.chapter_frame_count - 1));
    }
    model.progress_percent = std::clamp(model.progress_percent, 0, 100);

    switch (model.mode) {
    case ReaderVisualMode::sleep:
        model.primary_text = "Screen sleeping";
        break;
    case ReaderVisualMode::title:
        model.primary_text = state.chapter_title.empty() ? "Document" : state.chapter_title;
        break;
    case ReaderVisualMode::rsvp:
        model.primary_text = state.visible_frame ? state.visible_frame->text_utf8 : "";
        break;
    case ReaderVisualMode::context:
        model.primary_text = "Sentence context";
        {
            std::size_t last_source_token = static_cast<std::size_t>(-1);
            for (const auto& frame : view.context_frames) {
                const auto text = context_chunk_text(frame);
                if (frame.source_token_index == last_source_token &&
                    !model.context_words.empty()) {
                    model.context_words.back().text += text;
                    continue;
                }
                last_source_token = frame.source_token_index;
                model.context_words.push_back(
                    {text,
                     frame.source_token_index == view.context.current_source_token_index});
            }
        }
        break;
    }

    return model;
}

} // namespace rsvp::ui

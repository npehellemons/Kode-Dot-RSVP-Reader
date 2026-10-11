#pragma once

#include "rsvp/controller/reader_controller.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace rsvp::ui {

enum class ReaderVisualMode {
    title,
    rsvp,
    context,
    sleep,
};

struct ContextWordView {
    std::string text;
    bool current = false;
};

struct ReaderViewModel {
    ReaderVisualMode mode = ReaderVisualMode::title;
    std::string primary_text;
    std::vector<ContextWordView> context_words;
    std::string chapter_title;
    std::size_t chapter_index = 0;
    std::size_t chapter_count = 0;
    std::size_t chapter_position = 0;
    std::size_t chapter_total = 0;
    std::size_t frame_position = 0;
    std::size_t frame_total = 0;
    int progress_percent = 0;
    std::size_t orp_codepoint_index = 0;
    int font_scale_percent = 100;
    int wpm = 250;
    std::uint64_t remaining_seconds = 0;
    std::uint64_t chapter_remaining_seconds = 0;
    bool playing = false;
    bool paused = false;
    bool finished = false;
};

ReaderViewModel make_reader_view_model(const rsvp::controller::ReaderView& view);

} // namespace rsvp::ui

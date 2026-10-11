#pragma once

#include "rsvp/ui/reader_view_model.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace kode_dot {

enum class ReaderScreenMode {
    title,
    rsvp,
    context,
    sleep,
};

struct ReaderScreenWord {
    std::string text;
    bool current = false;
};

struct ReaderScreenFrame {
    ReaderScreenMode mode = ReaderScreenMode::title;
    std::string primary_text;
    std::vector<ReaderScreenWord> context_words;
    std::string chapter_label;
    std::string position_label;
    std::string wpm_label;
    std::string remaining_label;
    int progress_percent = 0;
    std::size_t orp_codepoint_index = 0;
    int font_scale_percent = 100;
    bool playing = false;
    bool sleeping = false;
};

ReaderScreenMode screen_mode_from(rsvp::ui::ReaderVisualMode mode);
std::string format_screen_duration(std::uint64_t seconds);
ReaderScreenFrame make_reader_screen_frame(const rsvp::ui::ReaderViewModel& model);

} // namespace kode_dot

#include "kode_dot/reader_screen.hpp"

#include <algorithm>
#include <cstdint>
#include <string>

namespace kode_dot {
namespace {

std::string two_digit(std::uint64_t value) {
    return value < 10 ? "0" + std::to_string(value) : std::to_string(value);
}

} // namespace

ReaderScreenMode screen_mode_from(rsvp::ui::ReaderVisualMode mode) {
    switch (mode) {
    case rsvp::ui::ReaderVisualMode::title:
        return ReaderScreenMode::title;
    case rsvp::ui::ReaderVisualMode::rsvp:
        return ReaderScreenMode::rsvp;
    case rsvp::ui::ReaderVisualMode::context:
        return ReaderScreenMode::context;
    case rsvp::ui::ReaderVisualMode::sleep:
        return ReaderScreenMode::sleep;
    }
    return ReaderScreenMode::rsvp;
}

std::string format_screen_duration(std::uint64_t seconds) {
    const auto hours = seconds / 3600U;
    const auto minutes = (seconds % 3600U) / 60U;
    const auto remainder = seconds % 60U;
    if (hours > 0) {
        return std::to_string(hours) + ":" + two_digit(minutes) + ":" +
               two_digit(remainder);
    }
    return std::to_string(minutes) + ":" + two_digit(remainder);
}

ReaderScreenFrame make_reader_screen_frame(const rsvp::ui::ReaderViewModel& model) {
    ReaderScreenFrame frame;
    frame.mode = screen_mode_from(model.mode);
    frame.primary_text = model.primary_text;
    frame.chapter_label = "Ch " + std::to_string(model.chapter_index) + "/" +
                          std::to_string(model.chapter_count);
    frame.position_label = std::to_string(model.chapter_position) + "/" +
                           std::to_string(model.chapter_total);
    frame.wpm_label = std::to_string(model.wpm) + " WPM";
    frame.remaining_label = format_screen_duration(model.chapter_remaining_seconds);
    frame.progress_percent = std::clamp(model.progress_percent, 0, 100);
    frame.orp_codepoint_index = model.orp_codepoint_index;
    frame.font_scale_percent = model.font_scale_percent;
    frame.playing = model.playing;
    frame.sleeping = model.mode == rsvp::ui::ReaderVisualMode::sleep;

    frame.context_words.reserve(model.context_words.size());
    for (const auto& word : model.context_words) {
        frame.context_words.push_back({word.text, word.current});
    }
    return frame;
}

} // namespace kode_dot

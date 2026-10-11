#include "rsvp/controller/reader_controller.hpp"
#include "rsvp/text/display_frame.hpp"
#include "rsvp/text/tokenizer.hpp"
#include "rsvp/ui/reader_view_model.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

using rsvp::controller::ReaderController;
using rsvp::controller::ReaderInput;
using rsvp::playback::PlaybackSettings;
using rsvp::text::make_display_frames;
using rsvp::text::tokenize;
using rsvp::ui::ReaderVisualMode;
using rsvp::ui::make_reader_view_model;

enum class StepKind {
    input,
    tick,
};

struct TraceStep {
    std::string_view label;
    StepKind kind = StepKind::input;
    ReaderInput input = ReaderInput::red;
    std::uint64_t at_ms = 0;
};

struct TraceScenario {
    std::string_view name;
    std::vector<TraceStep> steps;
};

void write_json_string(std::ostream& out, std::string_view value) {
    out << '"';
    for (const unsigned char byte : value) {
        switch (byte) {
        case '"':
            out << "\\\"";
            break;
        case '\\':
            out << "\\\\";
            break;
        case '\b':
            out << "\\b";
            break;
        case '\f':
            out << "\\f";
            break;
        case '\n':
            out << "\\n";
            break;
        case '\r':
            out << "\\r";
            break;
        case '\t':
            out << "\\t";
            break;
        default:
            if (byte < 0x20) {
                static constexpr char kHex[] = "0123456789abcdef";
                out << "\\u00" << kHex[(byte >> 4U) & 0x0FU] << kHex[byte & 0x0FU];
            } else {
                out << static_cast<char>(byte);
            }
            break;
        }
    }
    out << '"';
}

std::string_view visual_mode_name(ReaderVisualMode mode) {
    switch (mode) {
    case ReaderVisualMode::title:
        return "title";
    case ReaderVisualMode::rsvp:
        return "rsvp";
    case ReaderVisualMode::context:
        return "context";
    case ReaderVisualMode::sleep:
        return "sleep";
    }
    return "rsvp";
}

std::vector<TraceScenario> scenarios() {
    return {
        {
            "reader-flow",
            {
                {"red-starts", StepKind::input, ReaderInput::red, 0},
                {"tick-next-word", StepKind::tick, ReaderInput::red, 240},
                {"dpad-right-pauses", StepKind::input, ReaderInput::dpad_right, 260},
                {"hold-left-context", StepKind::input, ReaderInput::hold_left, 620},
                {"hold-end-rsvp", StepKind::input, ReaderInput::hold_end, 700},
                {"sleep", StepKind::input, ReaderInput::sleep, 800},
                {"red-wake-title", StepKind::input, ReaderInput::red, 900},
            },
        },
    };
}

ReaderController make_controller() {
    constexpr std::string_view kText =
        "One supercalifragilistic! Three four five.";
    return ReaderController(make_display_frames(tokenize(kText)),
                            {},
                            PlaybackSettings{.wpm = 250});
}

void write_view_model(std::ostream& out,
                      std::string_view label,
                      std::uint64_t at_ms,
                      const ReaderController& controller,
                      bool handled) {
    const auto model = make_reader_view_model(controller.view());

    out << "        {\n";
    out << "          \"label\": ";
    write_json_string(out, label);
    out << ",\n";
    out << "          \"at_ms\": " << at_ms << ",\n";
    out << "          \"handled\": " << (handled ? "true" : "false") << ",\n";
    out << "          \"mode\": ";
    write_json_string(out, visual_mode_name(model.mode));
    out << ",\n";
    out << "          \"primary_text\": ";
    write_json_string(out, model.primary_text);
    out << ",\n";
    out << "          \"chapter_title\": ";
    write_json_string(out, model.chapter_title);
    out << ",\n";
    out << "          \"chapter_index\": " << model.chapter_index << ",\n";
    out << "          \"chapter_count\": " << model.chapter_count << ",\n";
    out << "          \"chapter_position\": " << model.chapter_position << ",\n";
    out << "          \"chapter_total\": " << model.chapter_total << ",\n";
    out << "          \"frame_position\": " << model.frame_position << ",\n";
    out << "          \"frame_total\": " << model.frame_total << ",\n";
    out << "          \"progress_percent\": " << model.progress_percent << ",\n";
    out << "          \"orp_codepoint_index\": " << model.orp_codepoint_index << ",\n";
    out << "          \"font_scale_percent\": " << model.font_scale_percent << ",\n";
    out << "          \"wpm\": " << model.wpm << ",\n";
    out << "          \"remaining_seconds\": " << model.remaining_seconds << ",\n";
    out << "          \"chapter_remaining_seconds\": " << model.chapter_remaining_seconds << ",\n";
    out << "          \"playing\": " << (model.playing ? "true" : "false") << ",\n";
    out << "          \"paused\": " << (model.paused ? "true" : "false") << ",\n";
    out << "          \"finished\": " << (model.finished ? "true" : "false") << ",\n";
    out << "          \"context_words\": [";
    for (std::size_t index = 0; index < model.context_words.size(); ++index) {
        const auto& word = model.context_words[index];
        if (index != 0) {
            out << ", ";
        }
        out << "{\"text\": ";
        write_json_string(out, word.text);
        out << ", \"current\": " << (word.current ? "true" : "false") << "}";
    }
    out << "]\n";
    out << "        }";
}

void write_scenario(std::ostream& out,
                    const TraceScenario& scenario,
                    bool is_last) {
    auto controller = make_controller();

    out << "    {\n";
    out << "      \"name\": ";
    write_json_string(out, scenario.name);
    out << ",\n";
    out << "      \"events\": [\n";
    write_view_model(out, "initial", 0, controller, true);

    for (const auto& step : scenario.steps) {
        bool handled = false;
        if (step.kind == StepKind::input) {
            handled = controller.handle_input(step.input, step.at_ms);
        } else {
            handled = controller.tick(step.at_ms);
        }

        out << ",\n";
        write_view_model(out, step.label, step.at_ms, controller, handled);
    }

    out << "\n      ]\n";
    out << "    }" << (is_last ? "" : ",") << '\n';
}

} // namespace

int main() {
    const auto all_scenarios = scenarios();

    std::cout << "{\n";
    std::cout << "  \"schema\": \"rsvp-view-model-trace-v1\",\n";
    std::cout << "  \"controller_schema\": \"rsvp-reader-controller-v1\",\n";
    std::cout << "  \"fixture_text\": \"One supercalifragilistic! Three four five.\",\n";
    std::cout << "  \"traces\": [\n";
    for (std::size_t index = 0; index < all_scenarios.size(); ++index) {
        write_scenario(std::cout, all_scenarios[index], index + 1 == all_scenarios.size());
    }
    std::cout << "  ]\n";
    std::cout << "}\n";
    return 0;
}

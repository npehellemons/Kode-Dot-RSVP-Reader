#include "rsvp/controller/reader_controller.hpp"
#include "rsvp/text/display_frame.hpp"
#include "rsvp/text/tokenizer.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

using rsvp::controller::ReaderController;
using rsvp::controller::ReaderDisplayMode;
using rsvp::controller::ReaderInput;
using rsvp::playback::PlaybackSettings;
using rsvp::playback::PlaybackState;
using rsvp::text::make_display_frames;
using rsvp::text::tokenize;

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

std::string_view mode_name(ReaderDisplayMode mode) {
    switch (mode) {
    case ReaderDisplayMode::title:
        return "title";
    case ReaderDisplayMode::rsvp:
        return "rsvp";
    case ReaderDisplayMode::context:
        return "context";
    case ReaderDisplayMode::sleep:
        return "sleep";
    }
    return "rsvp";
}

std::string_view playback_state_name(PlaybackState state) {
    switch (state) {
    case PlaybackState::idle:
        return "idle";
    case PlaybackState::playing:
        return "playing";
    case PlaybackState::paused:
        return "paused";
    case PlaybackState::finished:
        return "finished";
    }
    return "idle";
}

std::string_view input_name(ReaderInput input) {
    switch (input) {
    case ReaderInput::red:
        return "red";
    case ReaderInput::blue:
        return "blue";
    case ReaderInput::dpad_left:
        return "dpad_left";
    case ReaderInput::dpad_right:
        return "dpad_right";
    case ReaderInput::dpad_up:
        return "dpad_up";
    case ReaderInput::dpad_down:
        return "dpad_down";
    case ReaderInput::hold_left:
        return "hold_left";
    case ReaderInput::hold_right:
        return "hold_right";
    case ReaderInput::hold_end:
        return "hold_end";
    case ReaderInput::sleep:
        return "sleep";
    }
    return "red";
}

void write_view(std::ostream& out,
                std::string_view label,
                std::uint64_t at_ms,
                const ReaderController& controller,
                bool handled) {
    const auto view = controller.view();
    const auto& playback = view.playback;

    out << "        {\n";
    out << "          \"label\": ";
    write_json_string(out, label);
    out << ",\n";
    out << "          \"at_ms\": " << at_ms << ",\n";
    out << "          \"handled\": " << (handled ? "true" : "false") << ",\n";
    out << "          \"mode\": ";
    write_json_string(out, mode_name(view.mode));
    out << ",\n";
    out << "          \"playback_state\": ";
    write_json_string(out, playback_state_name(playback.state));
    out << ",\n";
    out << "          \"frame_index\": " << playback.frame_index << ",\n";
    out << "          \"source_token_index\": " << playback.source_token_index << ",\n";
    out << "          \"wpm\": " << playback.wpm << ",\n";
    out << "          \"visible_text\": ";
    write_json_string(out, playback.visible_frame ? playback.visible_frame->text_utf8 : "");
    out << ",\n";
    out << "          \"context_start_frame_index\": " << view.context.start_frame_index << ",\n";
    out << "          \"context_end_frame_index\": " << view.context.end_frame_index << "\n";
    out << "        }";
}

std::vector<TraceScenario> scenarios() {
    return {
        {
            "red-play-pause",
            {
                {"red-starts", StepKind::input, ReaderInput::red, 0},
                {"red-pauses", StepKind::input, ReaderInput::red, 50},
                {"red-resumes", StepKind::input, ReaderInput::red, 100},
            },
        },
        {
            "sleep-red-wake",
            {
                {"red-starts", StepKind::input, ReaderInput::red, 0},
                {"tick-next-word", StepKind::tick, ReaderInput::red, 240},
                {"red-pauses", StepKind::input, ReaderInput::red, 250},
                {"sleep", StepKind::input, ReaderInput::sleep, 300},
                {"late-tick-while-asleep", StepKind::tick, ReaderInput::red, 5000},
                {"red-wakes-title", StepKind::input, ReaderInput::red, 5100},
                {"red-resumes", StepKind::input, ReaderInput::red, 5200},
                {"red-pauses-again", StepKind::input, ReaderInput::red, 5300},
            },
        },
        {
            "dpad-step",
            {
                {"red-starts", StepKind::input, ReaderInput::red, 0},
                {"right-pauses-next-word", StepKind::input, ReaderInput::dpad_right, 10},
                {"right-pauses-next-word-again", StepKind::input, ReaderInput::dpad_right, 20},
                {"left-pauses-previous-word", StepKind::input, ReaderInput::dpad_left, 30},
            },
        },
        {
            "hold-context",
            {
                {"red-starts", StepKind::input, ReaderInput::red, 0},
                {"hold-right-enters-context", StepKind::input, ReaderInput::hold_right, 10},
                {"hold-right-repeats-context", StepKind::input, ReaderInput::hold_right, 430},
                {"hold-end-returns-rsvp", StepKind::input, ReaderInput::hold_end, 500},
            },
        },
        {
            "wpm-bounds",
            {
                {"wpm-up", StepKind::input, ReaderInput::dpad_up, 0},
                {"wpm-up-again", StepKind::input, ReaderInput::dpad_up, 10},
                {"wpm-down", StepKind::input, ReaderInput::dpad_down, 20},
                {"wpm-down-again", StepKind::input, ReaderInput::dpad_down, 30},
                {"wpm-down-third", StepKind::input, ReaderInput::dpad_down, 40},
            },
        },
    };
}

ReaderController make_controller() {
    constexpr std::string_view kText =
        "One two three. Four five six. Seven eight nine.";
    return ReaderController(make_display_frames(tokenize(kText)),
                            {},
                            PlaybackSettings{.wpm = 250});
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
    write_view(out, "initial", 0, controller, true);

    for (const auto& step : scenario.steps) {
        bool handled = false;
        if (step.kind == StepKind::input) {
            handled = controller.handle_input(step.input, step.at_ms);
        } else {
            handled = controller.tick(step.at_ms);
        }

        out << ",\n";
        write_view(out, step.label, step.at_ms, controller, handled);
    }

    out << "\n      ]\n";
    out << "    }" << (is_last ? "" : ",") << '\n';
}

} // namespace

int main() {
    const auto all_scenarios = scenarios();

    std::cout << "{\n";
    std::cout << "  \"schema\": \"rsvp-controller-trace-v1\",\n";
    std::cout << "  \"controller_schema\": \"rsvp-reader-controller-v1\",\n";
    std::cout << "  \"fixture_text\": \"One two three. Four five six. Seven eight nine.\",\n";
    std::cout << "  \"traces\": [\n";
    for (std::size_t index = 0; index < all_scenarios.size(); ++index) {
        write_scenario(std::cout, all_scenarios[index], index + 1 == all_scenarios.size());
    }
    std::cout << "  ]\n";
    std::cout << "}\n";
    return 0;
}

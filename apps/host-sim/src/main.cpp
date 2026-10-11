#include "rsvp/library/document_text.hpp"
#include "rsvp/runtime/reader_runtime.hpp"
#include "rsvp/storage/state_store.hpp"
#include "rsvp/text/display_frame.hpp"
#include "rsvp/text/tokenizer.hpp"
#include "rsvp/ui/reader_view_model.hpp"

#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <thread>

#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

namespace {

using rsvp::controller::ReaderView;
using rsvp::playback::PlaybackSettings;
using rsvp::playback::PlaybackState;
using rsvp::lifecycle::DocumentIdentity;
using rsvp::lifecycle::ReaderStatePaths;
using rsvp::library::import_txt_document;
using rsvp::library::make_frame_chapters;
using rsvp::platform::ControlEvent;
using rsvp::platform::ControlPhase;
using rsvp::platform::LifecycleEvent;
using rsvp::platform::LifecycleSignal;
using rsvp::platform::PhysicalControl;
using rsvp::runtime::ReaderRuntime;
using rsvp::storage::content_hash;
using rsvp::text::make_display_frames;
using rsvp::text::tokenize;
using rsvp::ui::ReaderViewModel;
using rsvp::ui::ReaderVisualMode;
using rsvp::ui::make_reader_view_model;

constexpr const char* kDocId = "bundled-sample-preview";
constexpr const char* kSampleText =
    "Welcome to the Kode Dot RSVP Reader.\n\n"
    "This first preview runs entirely on the host machine, but it uses the same "
    "tokenizer and playback engine intended for the device. Pause whenever you "
    "want, step backward, tune the pace, and keep the reading flow calm.";

volatile std::sig_atomic_t interrupted = 0;

void handle_signal(int) {
    interrupted = 1;
}

struct TerminalSize {
    int columns = 80;
    int rows = 24;
};

class TerminalMode {
public:
    TerminalMode() {
        if (!isatty(STDIN_FILENO)) {
            return;
        }
        if (tcgetattr(STDIN_FILENO, &original_) != 0) {
            return;
        }

        auto raw = original_;
        raw.c_lflag &= static_cast<unsigned long>(~(ICANON | ECHO));
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        enabled_ = tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0;
    }

    ~TerminalMode() {
        if (enabled_) {
            tcsetattr(STDIN_FILENO, TCSANOW, &original_);
        }
    }

    TerminalMode(const TerminalMode&) = delete;
    TerminalMode& operator=(const TerminalMode&) = delete;

private:
    termios original_{};
    bool enabled_ = false;
};

std::uint64_t monotonic_ms() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

std::optional<char> read_key() {
    fd_set set;
    FD_ZERO(&set);
    FD_SET(STDIN_FILENO, &set);

    timeval timeout{};
    const int ready = select(STDIN_FILENO + 1, &set, nullptr, nullptr, &timeout);
    if (ready <= 0 || !FD_ISSET(STDIN_FILENO, &set)) {
        return std::nullopt;
    }

    char ch = 0;
    if (read(STDIN_FILENO, &ch, 1) == 1) {
        return ch;
    }
    return std::nullopt;
}

std::filesystem::path state_dir() {
    if (const char* override_dir = std::getenv("KODE_RSVP_PREVIEW_STATE_DIR")) {
        if (override_dir[0] != '\0') {
            return override_dir;
        }
    }
    return std::filesystem::path("build-host") / "host-preview-state";
}

std::uint64_t wall_time_seconds() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(now).count());
}

TerminalSize terminal_size() {
    winsize size{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 &&
        size.ws_col > 0 && size.ws_row > 0) {
        return {size.ws_col, size.ws_row};
    }
    return {};
}

const char* state_label(PlaybackState state) {
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
    return "unknown";
}

const char* mode_label(ReaderVisualMode mode) {
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
    return "unknown";
}

void clear_screen() {
    std::cout << "\x1b[2J\x1b[H";
}

std::string repeat(char ch, int count) {
    if (count <= 0) {
        return {};
    }
    return std::string(static_cast<std::size_t>(count), ch);
}

std::string truncate_to_columns(const std::string& text, int columns) {
    if (columns <= 0 || static_cast<int>(text.size()) <= columns) {
        return text;
    }
    if (columns <= 3) {
        return text.substr(0, static_cast<std::size_t>(columns));
    }
    return text.substr(0, static_cast<std::size_t>(columns - 3)) + "...";
}

void print_centered(const std::string& text, int columns) {
    const auto clamped = truncate_to_columns(text, columns);
    const int padding = (columns - static_cast<int>(clamped.size())) / 2;
    std::cout << repeat(' ', padding) << clamped << '\n';
}

std::string progress_bar(std::size_t index, std::size_t count, int width) {
    if (width <= 2) {
        return "[]";
    }

    const int inner_width = width - 2;
    int filled = 0;
    if (count > 0) {
        const auto completed = index + 1;
        filled = static_cast<int>((completed * static_cast<std::size_t>(inner_width)) / count);
    }
    if (filled < 0) {
        filled = 0;
    }
    if (filled > inner_width) {
        filled = inner_width;
    }
    return "[" + repeat('#', filled) + repeat('-', inner_width - filled) + "]";
}

void render_context(const ReaderViewModel& model, int columns) {
    print_centered(model.primary_text, columns);
    std::cout << '\n';

    std::string line;
    for (const auto& word : model.context_words) {
        if (word.current) {
            line += "[" + word.text + "] ";
        } else {
            line += word.text + " ";
        }
    }
    print_centered(line, columns);
}

void render(const ReaderView& view) {
    clear_screen();

    const auto model = make_reader_view_model(view);
    const auto& state = view.playback;
    const auto size = terminal_size();
    const int content_width = size.columns > 10 ? size.columns - 4 : size.columns;
    const int top_padding = size.rows > 14 ? (size.rows - 12) / 2 : 1;
    const int bar_width = content_width < 50 ? content_width : 50;

    std::cout << repeat('\n', top_padding);
    print_centered("Kode Dot RSVP Reader", size.columns);
    std::cout << '\n';

    if (model.mode == ReaderVisualMode::context) {
        render_context(model, size.columns);
    } else {
        print_centered(model.primary_text, size.columns);
    }
    std::cout << '\n';
    print_centered(progress_bar(model.chapter_position == 0 ? 0 : model.chapter_position - 1,
                                model.chapter_total,
                                bar_width),
                   size.columns);
    std::cout << '\n';

    const std::string status =
        std::string("Mode: ") + mode_label(model.mode) +
        "    State: " + state_label(state.state) +
        "    WPM: " + std::to_string(model.wpm) +
        "    Token: " + std::to_string(model.frame_position) + "/" +
        std::to_string(model.frame_total) +
        "    Remaining: " + std::to_string(model.remaining_seconds) + "s";
    print_centered(status, size.columns);
    std::cout << '\n';
    const std::string chapter =
        "Chapter: " + model.chapter_title + "    " +
        std::to_string(model.chapter_position) + "/" +
        std::to_string(model.chapter_total) +
        "    Remaining: " + std::to_string(model.chapter_remaining_seconds) + "s";
    print_centered(chapter, size.columns);
    std::cout << '\n';
    print_centered("Space red play/pause/wake   h/l word step   j/k WPM   s sleep   q quit",
                   size.columns);
    std::cout.flush();
}

std::optional<ControlEvent> control_event_for_key(char key, std::uint64_t now) {
    switch (key) {
    case ' ':
        return ControlEvent{PhysicalControl::red_button, ControlPhase::release, now};
    case 'h':
    case 'H':
        return ControlEvent{PhysicalControl::dpad_left, ControlPhase::release, now};
    case 'l':
    case 'L':
        return ControlEvent{PhysicalControl::dpad_right, ControlPhase::release, now};
    case 'j':
    case 'J':
        return ControlEvent{PhysicalControl::dpad_down, ControlPhase::release, now};
    case 'k':
    case 'K':
        return ControlEvent{PhysicalControl::dpad_up, ControlPhase::release, now};
    default:
        return std::nullopt;
    }
}

} // namespace

int main() {
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    TerminalMode terminal_mode;

    const auto dir = state_dir();
    const auto preview_document =
        import_txt_document(kSampleText, "Kode Dot RSVP Reader - Preview.txt");
    const auto source_hash = content_hash(preview_document.normalized_text_utf8);
    const DocumentIdentity document{kDocId, source_hash};
    const ReaderStatePaths state_paths{dir / "settings.state", dir / "bookmark.state"};
    const auto tokens = tokenize(preview_document.reading_text_utf8);
    auto frames = make_display_frames(tokens);
    auto chapters = make_frame_chapters(preview_document, frames);

    auto runtime = ReaderRuntime::restore(frames, chapters, state_paths, document,
                                          PlaybackSettings{}, monotonic_ms());
    render(runtime.view());

    bool running = true;
    while (running && !interrupted) {
        if (const auto key = read_key()) {
            const auto now = monotonic_ms();
            if (*key == 'q' || *key == 'Q') {
                running = false;
            } else if (*key == 's' || *key == 'S') {
                runtime.handle_lifecycle_event(
                    LifecycleEvent{LifecycleSignal::sleep, now}, wall_time_seconds());
                render(runtime.view());
            } else if (const auto event = control_event_for_key(*key, now)) {
                if (runtime.handle_control_event(*event, wall_time_seconds())) {
                    render(runtime.view());
                }
            }
        }

        if (runtime.tick(monotonic_ms())) {
            render(runtime.view());
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    runtime.persist(wall_time_seconds());
    clear_screen();
    std::cout << "RSVP preview closed.\n";
    return 0;
}

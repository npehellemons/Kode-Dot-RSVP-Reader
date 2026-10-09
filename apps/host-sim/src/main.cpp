#include "rsvp/playback/display_adapter.hpp"
#include "rsvp/text/tokenizer.hpp"

#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <thread>

#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

namespace {

using rsvp::playback::DisplayAdapter;
using rsvp::playback::DisplayState;
using rsvp::playback::PlaybackSettings;
using rsvp::playback::PlaybackState;
using rsvp::text::tokenize;

constexpr const char* kSampleText =
    "Welcome to the Kode Dot RSVP Reader.\n\n"
    "This first preview runs entirely on the host machine, but it uses the same "
    "tokenizer and playback engine intended for the device. Pause whenever you "
    "want, step backward, tune the pace, and keep the reading flow calm.";

volatile std::sig_atomic_t interrupted = 0;

void handle_signal(int) {
    interrupted = 1;
}

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

void clear_screen() {
    std::cout << "\x1b[2J\x1b[H";
}

void render(const DisplayState& state) {
    clear_screen();

    const auto word = state.visible_token ? state.visible_token->text_utf8 : std::string{};
    const auto shown_index = state.token_count == 0 ? 0 : state.token_index + 1;
    const auto remaining_seconds = (state.estimated_remaining_ms + 999) / 1000;

    std::cout << "\n\n";
    std::cout << "                         " << word << "\n\n";
    std::cout << "  State: " << state_label(state.state)
              << "    WPM: " << state.wpm
              << "    Token: " << shown_index << "/" << state.token_count
              << "    Remaining: " << remaining_seconds << "s\n\n";
    std::cout << "  Space play/pause   h rewind   l forward   j/k WPM   r restart   q quit\n";
    std::cout.flush();
}

void toggle_playback(DisplayAdapter& adapter) {
    const auto now = monotonic_ms();
    switch (adapter.snapshot().state) {
    case PlaybackState::idle:
        adapter.start(now);
        break;
    case PlaybackState::playing:
        adapter.pause();
        break;
    case PlaybackState::paused:
        adapter.resume(now);
        break;
    case PlaybackState::finished:
        adapter.restart(now);
        break;
    }
}

} // namespace

int main() {
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    TerminalMode terminal_mode;

    DisplayAdapter adapter(
        tokenize(kSampleText), PlaybackSettings{.wpm = 250},
        [](const DisplayState& state) { render(state); });

    adapter.emit_current();

    bool running = true;
    while (running && !interrupted) {
        if (const auto key = read_key()) {
            switch (*key) {
            case ' ':
                toggle_playback(adapter);
                break;
            case 'h':
            case 'H':
                adapter.rewind_one(monotonic_ms());
                break;
            case 'l':
            case 'L':
                adapter.advance_one(monotonic_ms());
                break;
            case 'j':
            case 'J':
                adapter.set_wpm(adapter.snapshot().wpm - 25);
                break;
            case 'k':
            case 'K':
                adapter.set_wpm(adapter.snapshot().wpm + 25);
                break;
            case 'r':
            case 'R':
                adapter.restart(monotonic_ms());
                break;
            case 'q':
            case 'Q':
                running = false;
                break;
            default:
                break;
            }
        }

        adapter.tick(monotonic_ms());
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    clear_screen();
    std::cout << "RSVP preview closed.\n";
    return 0;
}

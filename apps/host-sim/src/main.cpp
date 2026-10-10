#include "rsvp/lifecycle/reader_lifecycle.hpp"
#include "rsvp/playback/display_adapter.hpp"
#include "rsvp/storage/state_store.hpp"
#include "rsvp/text/chapter.hpp"
#include "rsvp/text/display_frame.hpp"
#include "rsvp/text/tokenizer.hpp"

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

using rsvp::playback::DisplayAdapter;
using rsvp::playback::DisplayState;
using rsvp::playback::PlaybackSettings;
using rsvp::playback::PlaybackState;
using rsvp::lifecycle::DocumentIdentity;
using rsvp::lifecycle::ReaderStatePaths;
using rsvp::lifecycle::bookmark_matches_document;
using rsvp::lifecycle::capture_reader_state;
using rsvp::lifecycle::pause_and_persist_for_sleep;
using rsvp::lifecycle::persist_reader_state;
using rsvp::lifecycle::resolve_bookmark_frame;
using rsvp::lifecycle::to_playback_settings;
using rsvp::storage::content_hash;
using rsvp::storage::load_bookmark;
using rsvp::storage::load_settings;
using rsvp::text::make_chapters_from_markdown_headings;
using rsvp::text::make_display_frames;
using rsvp::text::tokenize;

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

void save_preview_state(const std::filesystem::path& dir,
                        const DisplayAdapter& adapter,
                        const DocumentIdentity& document) {
    persist_reader_state(
        {dir / "settings.state", dir / "bookmark.state"},
        capture_reader_state(document, adapter.snapshot(), adapter.settings(),
                             wall_time_seconds()));
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

void render(const DisplayState& state) {
    clear_screen();

    const auto size = terminal_size();
    const auto word = state.visible_token ? state.visible_token->text_utf8 : std::string{};
    const auto shown_index = state.token_count == 0 ? 0 : state.token_index + 1;
    const auto remaining_seconds = (state.estimated_remaining_ms + 999) / 1000;
    const auto chapter_remaining_seconds =
        (state.estimated_chapter_remaining_ms + 999) / 1000;
    const auto chapter_shown =
        state.chapter_frame_count == 0 ? 0 : state.chapter_frame_index + 1;
    const int content_width = size.columns > 10 ? size.columns - 4 : size.columns;
    const int top_padding = size.rows > 14 ? (size.rows - 12) / 2 : 1;
    const int bar_width = content_width < 50 ? content_width : 50;

    std::cout << repeat('\n', top_padding);
    print_centered("Kode Dot RSVP Reader", size.columns);
    std::cout << '\n';
    print_centered(word, size.columns);
    std::cout << '\n';
    print_centered(progress_bar(state.token_index, state.token_count, bar_width), size.columns);
    std::cout << '\n';

    const std::string status =
        std::string("State: ") + state_label(state.state) +
        "    WPM: " + std::to_string(state.wpm) +
        "    Token: " + std::to_string(shown_index) + "/" +
        std::to_string(state.token_count) +
        "    Remaining: " + std::to_string(remaining_seconds) + "s";
    print_centered(status, size.columns);
    std::cout << '\n';
    const std::string chapter =
        "Chapter: " + state.chapter_title + "    " +
        std::to_string(chapter_shown) + "/" +
        std::to_string(state.chapter_frame_count) +
        "    Remaining: " + std::to_string(chapter_remaining_seconds) + "s";
    print_centered(chapter, size.columns);
    std::cout << '\n';
    print_centered("Space red play/pause   h/l word step   j/k WPM   r sentence   s sleep   q quit",
                   size.columns);
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

    const auto dir = state_dir();
    const auto source_hash = content_hash(kSampleText);
    const DocumentIdentity document{kDocId, source_hash};
    const ReaderStatePaths state_paths{dir / "settings.state", dir / "bookmark.state"};
    const auto stored_settings = load_settings(dir / "settings.state");
    const auto playback_settings =
        stored_settings ? to_playback_settings(*stored_settings) : PlaybackSettings{};
    const auto tokens = tokenize(kSampleText);
    auto frames = make_display_frames(tokens);
    auto chapters = make_chapters_from_markdown_headings(kSampleText, frames,
                                                         "Preview");

    DisplayAdapter adapter(
        frames, chapters, playback_settings,
        [](const DisplayState& state) { render(state); });

    const auto stored_bookmark = load_bookmark(dir / "bookmark.state");
    if (stored_bookmark && bookmark_matches_document(*stored_bookmark, document)) {
        adapter.seek(resolve_bookmark_frame(*stored_bookmark, frames), monotonic_ms());
    }
    adapter.emit_current();

    bool running = true;
    while (running && !interrupted) {
        if (const auto key = read_key()) {
            switch (*key) {
            case ' ':
                toggle_playback(adapter);
                if (adapter.snapshot().state == PlaybackState::paused ||
                    adapter.snapshot().state == PlaybackState::finished) {
                    save_preview_state(dir, adapter, document);
                }
                break;
            case 'h':
            case 'H':
                adapter.rewind_word(monotonic_ms());
                save_preview_state(dir, adapter, document);
                break;
            case 'l':
            case 'L':
                adapter.advance_word(monotonic_ms());
                save_preview_state(dir, adapter, document);
                break;
            case 'j':
            case 'J':
                adapter.set_wpm(adapter.snapshot().wpm - 25);
                save_preview_state(dir, adapter, document);
                break;
            case 'k':
            case 'K':
                adapter.set_wpm(adapter.snapshot().wpm + 25);
                save_preview_state(dir, adapter, document);
                break;
            case 'r':
            case 'R':
                adapter.rewind_sentence(monotonic_ms());
                save_preview_state(dir, adapter, document);
                break;
            case 's':
            case 'S':
                pause_and_persist_for_sleep(adapter, state_paths, document, wall_time_seconds());
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

    save_preview_state(dir, adapter, document);
    clear_screen();
    std::cout << "RSVP preview closed.\n";
    return 0;
}

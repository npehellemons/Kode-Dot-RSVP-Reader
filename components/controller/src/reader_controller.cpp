#include "rsvp/controller/reader_controller.hpp"

#include <utility>

namespace rsvp::controller {

ReaderController::ReaderController(std::vector<rsvp::text::DisplayFrame> frames,
                                   std::vector<rsvp::text::Chapter> chapters,
                                   rsvp::playback::PlaybackSettings settings)
    : playback_(std::move(frames), std::move(chapters), settings) {
    if (playback_.snapshot().frame_count == 0) {
        mode_ = ReaderDisplayMode::rsvp;
    }
}

ReaderView ReaderController::view() const {
    ReaderView current;
    current.mode = mode_;
    current.playback = playback_.snapshot();
    current.context = playback_.current_sentence_context();
    return current;
}

const rsvp::playback::PlaybackEngine& ReaderController::playback() const {
    return playback_;
}

bool ReaderController::handle_input(ReaderInput input, std::uint64_t monotonic_ms) {
    if (mode_ == ReaderDisplayMode::sleep) {
        if (input == ReaderInput::red) {
            playback_.pause();
            mode_ = ReaderDisplayMode::title;
            return true;
        }
        return false;
    }

    switch (input) {
    case ReaderInput::red:
        return toggle_playback(monotonic_ms);
    case ReaderInput::blue:
        return false;
    case ReaderInput::dpad_left:
        playback_.rewind_word(monotonic_ms);
        show_rsvp();
        return true;
    case ReaderInput::dpad_right:
        playback_.advance_word(monotonic_ms);
        show_rsvp();
        return true;
    case ReaderInput::dpad_up:
        playback_.set_wpm(playback_.settings().wpm + 25);
        return true;
    case ReaderInput::dpad_down:
        playback_.set_wpm(playback_.settings().wpm - 25);
        return true;
    case ReaderInput::hold_left:
        playback_.rewind_word(monotonic_ms);
        show_context();
        return true;
    case ReaderInput::hold_right:
        playback_.advance_word(monotonic_ms);
        show_context();
        return true;
    case ReaderInput::hold_end:
        if (mode_ == ReaderDisplayMode::context) {
            show_rsvp();
            return true;
        }
        return false;
    case ReaderInput::sleep:
        playback_.pause();
        mode_ = ReaderDisplayMode::sleep;
        return true;
    }

    return false;
}

bool ReaderController::tick(std::uint64_t monotonic_ms) {
    if (mode_ != ReaderDisplayMode::rsvp) {
        return false;
    }
    return playback_.tick(monotonic_ms);
}

bool ReaderController::toggle_playback(std::uint64_t monotonic_ms) {
    show_rsvp();

    const auto state = playback_.snapshot().state;
    if (state == rsvp::playback::PlaybackState::playing) {
        playback_.pause();
        return true;
    }
    if (state == rsvp::playback::PlaybackState::finished) {
        playback_.restart(monotonic_ms);
        return true;
    }

    playback_.resume(monotonic_ms);
    return true;
}

void ReaderController::show_rsvp() {
    if (mode_ != ReaderDisplayMode::sleep) {
        mode_ = ReaderDisplayMode::rsvp;
    }
}

void ReaderController::show_context() {
    if (mode_ != ReaderDisplayMode::sleep) {
        mode_ = ReaderDisplayMode::context;
    }
}

} // namespace rsvp::controller

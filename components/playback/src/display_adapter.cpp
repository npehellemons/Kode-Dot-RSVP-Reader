#include "rsvp/playback/display_adapter.hpp"

namespace rsvp::playback {

DisplayAdapter::DisplayAdapter(std::vector<rsvp::text::Token> tokens,
                               PlaybackSettings settings,
                               Callback callback)
    : engine_(std::move(tokens), settings), callback_(std::move(callback)) {}

const DisplayState& DisplayAdapter::snapshot() const {
    return engine_.snapshot();
}

bool DisplayAdapter::emit_current() {
    return emit_if_changed();
}

bool DisplayAdapter::start(std::uint64_t monotonic_ms) {
    engine_.start(monotonic_ms);
    return emit_if_changed();
}

bool DisplayAdapter::pause() {
    engine_.pause();
    return emit_if_changed();
}

bool DisplayAdapter::resume(std::uint64_t monotonic_ms) {
    engine_.resume(monotonic_ms);
    return emit_if_changed();
}

bool DisplayAdapter::restart(std::uint64_t monotonic_ms) {
    engine_.restart(monotonic_ms);
    return emit_if_changed();
}

bool DisplayAdapter::seek(std::size_t token_index, std::uint64_t monotonic_ms) {
    engine_.seek(token_index, monotonic_ms);
    return emit_if_changed();
}

bool DisplayAdapter::rewind_one(std::uint64_t monotonic_ms) {
    engine_.rewind_one(monotonic_ms);
    return emit_if_changed();
}

bool DisplayAdapter::advance_one(std::uint64_t monotonic_ms) {
    engine_.advance_one(monotonic_ms);
    return emit_if_changed();
}

bool DisplayAdapter::set_wpm(int wpm) {
    engine_.set_wpm(wpm);
    return emit_if_changed();
}

bool DisplayAdapter::tick(std::uint64_t monotonic_ms) {
    engine_.tick(monotonic_ms);
    return emit_if_changed();
}

bool DisplayAdapter::emit_if_changed() {
    const auto& state = engine_.snapshot();
    if (!differs_from_last_emit(state)) {
        return false;
    }

    callback_(state);
    last_emitted_ = state;
    has_emitted_ = true;
    return true;
}

bool DisplayAdapter::differs_from_last_emit(const DisplayState& state) const {
    if (!has_emitted_) {
        return true;
    }

    if (state.state != last_emitted_.state ||
        state.token_index != last_emitted_.token_index ||
        state.token_count != last_emitted_.token_count ||
        state.wpm != last_emitted_.wpm ||
        state.estimated_remaining_ms != last_emitted_.estimated_remaining_ms ||
        state.visible_token.has_value() != last_emitted_.visible_token.has_value()) {
        return true;
    }

    if (!state.visible_token.has_value()) {
        return false;
    }

    const auto& current = *state.visible_token;
    const auto& previous = *last_emitted_.visible_token;
    return current.text_utf8 != previous.text_utf8 ||
           current.source_byte_start != previous.source_byte_start ||
           current.source_byte_length != previous.source_byte_length ||
           current.punctuation_flags != previous.punctuation_flags;
}

} // namespace rsvp::playback

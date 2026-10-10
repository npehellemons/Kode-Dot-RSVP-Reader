#include "rsvp/playback/display_adapter.hpp"

namespace rsvp::playback {

DisplayAdapter::DisplayAdapter(std::vector<rsvp::text::Token> tokens,
                               PlaybackSettings settings,
                               Callback callback)
    : engine_(std::move(tokens), settings), callback_(std::move(callback)) {}

DisplayAdapter::DisplayAdapter(std::vector<rsvp::text::DisplayFrame> frames,
                               PlaybackSettings settings,
                               Callback callback)
    : engine_(std::move(frames), settings), callback_(std::move(callback)) {}

const DisplayState& DisplayAdapter::snapshot() const {
    return engine_.snapshot();
}

const PlaybackSettings& DisplayAdapter::settings() const {
    return engine_.settings();
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
        state.frame_index != last_emitted_.frame_index ||
        state.frame_count != last_emitted_.frame_count ||
        state.source_token_index != last_emitted_.source_token_index ||
        state.chunk_index != last_emitted_.chunk_index ||
        state.chunk_count != last_emitted_.chunk_count ||
        state.wpm != last_emitted_.wpm ||
        state.estimated_remaining_ms != last_emitted_.estimated_remaining_ms ||
        state.visible_token.has_value() != last_emitted_.visible_token.has_value() ||
        state.visible_frame.has_value() != last_emitted_.visible_frame.has_value()) {
        return true;
    }

    if (state.visible_frame.has_value()) {
        const auto& current = *state.visible_frame;
        const auto& previous = *last_emitted_.visible_frame;
        if (current.text_utf8 != previous.text_utf8 ||
            current.source_token_index != previous.source_token_index ||
            current.chunk_index != previous.chunk_index ||
            current.chunk_count != previous.chunk_count ||
            current.source_byte_start != previous.source_byte_start ||
            current.source_byte_length != previous.source_byte_length ||
            current.orp_codepoint_index != previous.orp_codepoint_index ||
            current.centered_fallback != previous.centered_fallback ||
            current.continuation_before != previous.continuation_before ||
            current.continuation_after != previous.continuation_after ||
            current.punctuation_flags != previous.punctuation_flags) {
            return true;
        }
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

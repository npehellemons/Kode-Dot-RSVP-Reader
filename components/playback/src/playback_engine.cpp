#include "rsvp/playback/playback_engine.hpp"

#include <algorithm>
#include <limits>

namespace rsvp::playback {
namespace {

std::uint64_t ceil_div(std::uint64_t numerator, std::uint64_t denominator) {
    return (numerator + denominator - 1) / denominator;
}

std::uint64_t saturating_add(std::uint64_t lhs, std::uint64_t rhs) {
    if (std::numeric_limits<std::uint64_t>::max() - lhs < rhs) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return lhs + rhs;
}

} // namespace

PlaybackEngine::PlaybackEngine(std::vector<rsvp::text::Token> tokens,
                               PlaybackSettings settings)
    : tokens_(std::move(tokens)), settings_(settings) {
    set_wpm(settings_.wpm);
    snapshot_.state = PlaybackState::idle;
    snapshot_.token_index = 0;
    snapshot_.token_count = tokens_.size();
    update_visible_token();
    update_remaining();
}

const DisplayState& PlaybackEngine::snapshot() const {
    return snapshot_;
}

const PlaybackSettings& PlaybackEngine::settings() const {
    return settings_;
}

void PlaybackEngine::start(std::uint64_t monotonic_ms) {
    if (tokens_.empty()) {
        finish();
        return;
    }
    snapshot_.token_index = std::min(snapshot_.token_index, tokens_.size() - 1);
    snapshot_.state = PlaybackState::playing;
    update_visible_token();
    update_remaining();
    schedule_next_from(monotonic_ms);
}

void PlaybackEngine::pause() {
    if (snapshot_.state == PlaybackState::playing) {
        snapshot_.state = PlaybackState::paused;
        update_remaining();
    }
}

void PlaybackEngine::resume(std::uint64_t monotonic_ms) {
    if (tokens_.empty()) {
        finish();
        return;
    }
    if (snapshot_.state == PlaybackState::paused || snapshot_.state == PlaybackState::idle) {
        snapshot_.state = PlaybackState::playing;
        update_visible_token();
        update_remaining();
        schedule_next_from(monotonic_ms);
    }
}

void PlaybackEngine::restart(std::uint64_t monotonic_ms) {
    snapshot_.token_index = 0;
    if (tokens_.empty()) {
        finish();
        return;
    }
    snapshot_.state = PlaybackState::playing;
    update_visible_token();
    update_remaining();
    schedule_next_from(monotonic_ms);
}

void PlaybackEngine::seek(std::size_t token_index, std::uint64_t monotonic_ms) {
    if (tokens_.empty()) {
        snapshot_.token_index = 0;
        finish();
        return;
    }

    snapshot_.token_index = std::min(token_index, tokens_.size() - 1);
    if (snapshot_.state == PlaybackState::finished) {
        snapshot_.state = PlaybackState::paused;
    }
    update_visible_token();
    update_remaining();
    if (snapshot_.state == PlaybackState::playing) {
        schedule_next_from(monotonic_ms);
    }
}

void PlaybackEngine::rewind_one(std::uint64_t monotonic_ms) {
    if (tokens_.empty()) {
        return;
    }
    const auto target = snapshot_.token_index == 0 ? 0 : snapshot_.token_index - 1;
    seek(target, monotonic_ms);
}

void PlaybackEngine::advance_one(std::uint64_t monotonic_ms) {
    if (tokens_.empty()) {
        return;
    }
    if (snapshot_.token_index + 1 >= tokens_.size()) {
        snapshot_.token_index = tokens_.size() - 1;
        finish();
        return;
    }
    seek(snapshot_.token_index + 1, monotonic_ms);
}

bool PlaybackEngine::set_wpm(int wpm) {
    const auto bounded = std::clamp(wpm, kMinWpm, kMaxWpm);
    const bool accepted = bounded == wpm;
    settings_.wpm = bounded;
    update_remaining();
    return accepted;
}

void PlaybackEngine::set_punctuation_pause_mode(PunctuationPauseMode mode) {
    settings_.punctuation_pause_mode = mode;
    update_remaining();
}

bool PlaybackEngine::tick(std::uint64_t monotonic_ms) {
    if (snapshot_.state != PlaybackState::playing || tokens_.empty()) {
        return false;
    }
    if (monotonic_ms < next_due_ms_) {
        return false;
    }

    if (snapshot_.token_index + 1 >= tokens_.size()) {
        finish();
        return true;
    }

    ++snapshot_.token_index;
    update_visible_token();
    update_remaining();
    schedule_next_from(monotonic_ms);
    return true;
}

std::uint64_t PlaybackEngine::duration_for_index(std::size_t token_index) const {
    if (tokens_.empty() || token_index >= tokens_.size()) {
        return base_duration_ms();
    }

    const auto base = base_duration_ms();
    if (settings_.punctuation_pause_mode == PunctuationPauseMode::off) {
        return base;
    }

    const auto flags = tokens_[token_index].punctuation_flags;
    if (rsvp::text::has_flag(flags, rsvp::text::PunctuationFlags::sentence_end)) {
        return base * 2;
    }
    if (rsvp::text::has_flag(flags, rsvp::text::PunctuationFlags::comma)) {
        return ceil_div(base * 3, 2);
    }
    if (rsvp::text::has_flag(flags, rsvp::text::PunctuationFlags::paragraph_break_before)) {
        return ceil_div(base * 3, 2);
    }
    return base;
}

std::uint64_t PlaybackEngine::base_duration_ms() const {
    return ceil_div(60000, static_cast<std::uint64_t>(settings_.wpm));
}

void PlaybackEngine::update_visible_token() {
    if (tokens_.empty()) {
        snapshot_.visible_token.reset();
        return;
    }
    snapshot_.visible_token = tokens_[snapshot_.token_index];
}

void PlaybackEngine::update_remaining() {
    if (tokens_.empty() || snapshot_.token_index >= tokens_.size()) {
        snapshot_.estimated_remaining_ms = 0;
        return;
    }

    std::uint64_t total = 0;
    for (std::size_t i = snapshot_.token_index; i < tokens_.size(); ++i) {
        total = saturating_add(total, duration_for_index(i));
    }
    snapshot_.estimated_remaining_ms = total;
}

void PlaybackEngine::schedule_next_from(std::uint64_t monotonic_ms) {
    next_due_ms_ = saturating_add(monotonic_ms, duration_for_index(snapshot_.token_index));
}

void PlaybackEngine::finish() {
    snapshot_.state = PlaybackState::finished;
    update_visible_token();
    snapshot_.estimated_remaining_ms = 0;
}

} // namespace rsvp::playback

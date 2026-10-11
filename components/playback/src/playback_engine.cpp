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
    : PlaybackEngine(rsvp::text::make_display_frames(tokens), settings) {}

PlaybackEngine::PlaybackEngine(std::vector<rsvp::text::DisplayFrame> frames,
                               PlaybackSettings settings)
    : PlaybackEngine(std::move(frames), {}, settings) {}

PlaybackEngine::PlaybackEngine(std::vector<rsvp::text::DisplayFrame> frames,
                               std::vector<rsvp::text::Chapter> chapters,
                               PlaybackSettings settings)
    : frames_(std::move(frames)),
      chapters_(chapters.empty() ? rsvp::text::make_single_chapter(frames_) : std::move(chapters)),
      settings_(settings) {
    set_wpm(settings_.wpm);
    snapshot_.state = PlaybackState::idle;
    snapshot_.token_index = 0;
    snapshot_.token_count = frames_.size();
    snapshot_.frame_index = 0;
    snapshot_.frame_count = frames_.size();
    snapshot_.chapter_count = chapters_.size();
    snapshot_.wpm = settings_.wpm;
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
    if (frames_.empty()) {
        finish();
        return;
    }
    snapshot_.token_index = std::min(snapshot_.token_index, frames_.size() - 1);
    snapshot_.frame_index = snapshot_.token_index;
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
    if (frames_.empty()) {
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
    snapshot_.frame_index = 0;
    if (frames_.empty()) {
        finish();
        return;
    }
    snapshot_.state = PlaybackState::playing;
    update_visible_token();
    update_remaining();
    schedule_next_from(monotonic_ms);
}

void PlaybackEngine::seek(std::size_t frame_index, std::uint64_t monotonic_ms) {
    if (frames_.empty()) {
        snapshot_.token_index = 0;
        snapshot_.frame_index = 0;
        finish();
        return;
    }

    snapshot_.token_index = std::min(frame_index, frames_.size() - 1);
    snapshot_.frame_index = snapshot_.token_index;
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
    if (frames_.empty()) {
        return;
    }
    const auto target = snapshot_.token_index == 0 ? 0 : snapshot_.token_index - 1;
    seek(target, monotonic_ms);
}

void PlaybackEngine::advance_one(std::uint64_t monotonic_ms) {
    if (frames_.empty()) {
        return;
    }
    if (snapshot_.token_index + 1 >= frames_.size()) {
        snapshot_.token_index = frames_.size() - 1;
        snapshot_.frame_index = snapshot_.token_index;
        finish();
        return;
    }
    seek(snapshot_.token_index + 1, monotonic_ms);
}

void PlaybackEngine::rewind_word(std::uint64_t monotonic_ms) {
    if (frames_.empty()) {
        return;
    }
    seek_and_pause(previous_word_frame(), monotonic_ms);
}

void PlaybackEngine::advance_word(std::uint64_t monotonic_ms) {
    if (frames_.empty()) {
        return;
    }
    seek_and_pause(next_word_frame(), monotonic_ms);
}

void PlaybackEngine::rewind_sentence(std::uint64_t monotonic_ms) {
    if (frames_.empty()) {
        return;
    }

    const auto current_start = sentence_start_for_frame(snapshot_.frame_index);
    const auto target = snapshot_.frame_index == current_start
                            ? previous_sentence_start_before(current_start)
                            : current_start;
    seek_and_pause(target, monotonic_ms);
}

bool PlaybackEngine::set_wpm(int wpm) {
    const auto bounded = std::clamp(wpm, kMinWpm, kMaxWpm);
    const bool accepted = bounded == wpm;
    settings_.wpm = bounded;
    snapshot_.wpm = bounded;
    update_remaining();
    return accepted;
}

void PlaybackEngine::set_punctuation_pause_mode(PunctuationPauseMode mode) {
    settings_.punctuation_pause_mode = mode;
    update_remaining();
}

bool PlaybackEngine::tick(std::uint64_t monotonic_ms) {
    if (snapshot_.state != PlaybackState::playing || frames_.empty()) {
        return false;
    }
    if (monotonic_ms < next_due_ms_) {
        return false;
    }

    if (snapshot_.token_index + 1 >= frames_.size()) {
        finish();
        return true;
    }

    ++snapshot_.token_index;
    snapshot_.frame_index = snapshot_.token_index;
    update_visible_token();
    update_remaining();
    schedule_next_from(monotonic_ms);
    return true;
}

std::uint64_t PlaybackEngine::duration_for_index(std::size_t frame_index) const {
    if (frames_.empty() || frame_index >= frames_.size()) {
        return base_duration_ms();
    }

    const auto base = base_duration_ms();
    if (settings_.punctuation_pause_mode == PunctuationPauseMode::off) {
        return base;
    }

    const auto flags = frames_[frame_index].punctuation_flags;
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

SentenceContextWindow PlaybackEngine::sentence_context_for_frame(
    std::size_t frame_index) const {
    SentenceContextWindow context;
    if (frames_.empty()) {
        return context;
    }

    const auto clamped = std::min(frame_index, frames_.size() - 1);
    const auto current_start = sentence_start_for_frame(clamped);
    const auto current_end = sentence_end_for_frame(clamped);
    const auto previous_start = previous_sentence_start_before(current_start);
    const auto next_start = std::min(current_end + 1, frames_.size() - 1);
    const auto next_end = next_start > current_end ? sentence_end_for_frame(next_start)
                                                   : current_end;

    context.start_frame_index = previous_start;
    context.end_frame_index = std::max(current_end, next_end);
    context.current_frame_index = clamped;
    context.current_source_token_index = frames_[clamped].source_token_index;
    return context;
}

SentenceContextWindow PlaybackEngine::current_sentence_context() const {
    return sentence_context_for_frame(snapshot_.frame_index);
}

std::vector<rsvp::text::DisplayFrame> PlaybackEngine::frames_for_context(
    const SentenceContextWindow& context) const {
    std::vector<rsvp::text::DisplayFrame> frames;
    if (frames_.empty() || context.start_frame_index > context.end_frame_index) {
        return frames;
    }

    const auto start = std::min(context.start_frame_index, frames_.size() - 1);
    const auto end = std::min(context.end_frame_index, frames_.size() - 1);
    for (std::size_t index = start; index <= end; ++index) {
        frames.push_back(frames_[index]);
    }
    return frames;
}

FrameNavigation PlaybackEngine::navigation_for_frame(std::size_t frame_index) const {
    FrameNavigation navigation;
    if (frames_.empty()) {
        return navigation;
    }

    const auto clamped = std::min(frame_index, frames_.size() - 1);
    navigation.previous_word_frame_index = previous_word_frame_for_frame(clamped);
    navigation.next_word_frame_index = next_word_frame_for_frame(clamped);
    navigation.sentence_rewind_frame_index = sentence_rewind_frame_for_frame(clamped);

    for (const auto& chapter : chapters_) {
        if (chapter.first_frame_index == clamped) {
            navigation.starts_chapter = true;
            break;
        }
    }
    return navigation;
}

std::uint64_t PlaybackEngine::base_duration_ms() const {
    return ceil_div(60000, static_cast<std::uint64_t>(settings_.wpm));
}

void PlaybackEngine::seek_and_pause(std::size_t frame_index, std::uint64_t monotonic_ms) {
    seek(frame_index, monotonic_ms);
    if (!frames_.empty()) {
        snapshot_.state = PlaybackState::paused;
        update_remaining();
    }
}

std::size_t PlaybackEngine::first_frame_for_source_token(std::size_t source_token_index) const {
    for (std::size_t index = 0; index < frames_.size(); ++index) {
        if (frames_[index].source_token_index == source_token_index) {
            return index;
        }
    }
    return frames_.empty() ? std::size_t{0} : frames_.size() - 1;
}

std::size_t PlaybackEngine::previous_word_frame_for_frame(std::size_t frame_index) const {
    if (frames_.empty() || frame_index == 0) {
        return 0;
    }

    const auto clamped = std::min(frame_index, frames_.size() - 1);
    const auto current_source = frames_[clamped].source_token_index;
    for (std::size_t index = clamped; index > 0; --index) {
        const auto candidate = index - 1;
        if (frames_[candidate].source_token_index < current_source) {
            return first_frame_for_source_token(frames_[candidate].source_token_index);
        }
    }
    return 0;
}

std::size_t PlaybackEngine::next_word_frame_for_frame(std::size_t frame_index) const {
    if (frames_.empty()) {
        return 0;
    }

    const auto clamped = std::min(frame_index, frames_.size() - 1);
    const auto current_source = frames_[clamped].source_token_index;
    for (std::size_t index = clamped + 1; index < frames_.size(); ++index) {
        if (frames_[index].source_token_index > current_source) {
            return first_frame_for_source_token(frames_[index].source_token_index);
        }
    }
    return frames_.size() - 1;
}

std::size_t PlaybackEngine::sentence_rewind_frame_for_frame(std::size_t frame_index) const {
    if (frames_.empty()) {
        return 0;
    }

    const auto clamped = std::min(frame_index, frames_.size() - 1);
    const auto current_start = sentence_start_for_frame(clamped);
    return clamped == current_start ? previous_sentence_start_before(current_start)
                                    : current_start;
}

std::size_t PlaybackEngine::previous_word_frame() const {
    return previous_word_frame_for_frame(snapshot_.frame_index);
}

std::size_t PlaybackEngine::next_word_frame() const {
    return next_word_frame_for_frame(snapshot_.frame_index);
}

std::size_t PlaybackEngine::sentence_start_for_frame(std::size_t frame_index) const {
    if (frames_.empty()) {
        return 0;
    }

    const auto clamped = std::min(frame_index, frames_.size() - 1);
    for (std::size_t index = clamped; index > 0; --index) {
        const auto previous = index - 1;
        if (rsvp::text::has_flag(frames_[previous].punctuation_flags,
                                 rsvp::text::PunctuationFlags::sentence_end)) {
            return index;
        }
    }
    return 0;
}

std::size_t PlaybackEngine::sentence_end_for_frame(std::size_t frame_index) const {
    if (frames_.empty()) {
        return 0;
    }

    const auto clamped = std::min(frame_index, frames_.size() - 1);
    const auto chapter_index = chapter_index_for_frame(clamped);
    const auto chapter_end = chapters_.empty()
                                 ? frames_.size()
                                 : std::min(chapters_[chapter_index].first_frame_index +
                                                chapters_[chapter_index].frame_count,
                                            frames_.size());
    for (std::size_t index = clamped; index < chapter_end; ++index) {
        if (rsvp::text::has_flag(frames_[index].punctuation_flags,
                                 rsvp::text::PunctuationFlags::sentence_end)) {
            return index;
        }
    }
    return chapter_end == 0 ? std::size_t{0} : chapter_end - 1;
}

std::size_t PlaybackEngine::previous_sentence_start_before(std::size_t frame_index) const {
    if (frames_.empty() || frame_index == 0) {
        return 0;
    }

    const auto before = frame_index - 1;
    const auto previous_sentence_end =
        rsvp::text::has_flag(frames_[before].punctuation_flags,
                             rsvp::text::PunctuationFlags::sentence_end)
            ? before
            : sentence_start_for_frame(before);
    return sentence_start_for_frame(previous_sentence_end);
}

std::size_t PlaybackEngine::chapter_index_for_frame(std::size_t frame_index) const {
    if (chapters_.empty()) {
        return 0;
    }

    const auto clamped = frames_.empty() ? std::size_t{0}
                                         : std::min(frame_index, frames_.size() - 1);
    for (std::size_t index = 0; index < chapters_.size(); ++index) {
        const auto& chapter = chapters_[index];
        const auto begin = chapter.first_frame_index;
        const auto end = begin + chapter.frame_count;
        if (clamped >= begin && clamped < end) {
            return index;
        }
    }
    return chapters_.size() - 1;
}

void PlaybackEngine::update_visible_token() {
    if (frames_.empty()) {
        snapshot_.visible_token.reset();
        snapshot_.visible_frame.reset();
        snapshot_.chapter_index = 0;
        snapshot_.chapter_count = chapters_.size();
        snapshot_.chapter_frame_index = 0;
        snapshot_.chapter_frame_count = 0;
        snapshot_.chapter_title.clear();
        return;
    }
    const auto& frame = frames_[snapshot_.token_index];
    snapshot_.visible_frame = frame;
    snapshot_.source_token_index = frame.source_token_index;
    snapshot_.chunk_index = frame.chunk_index;
    snapshot_.chunk_count = frame.chunk_count;

    rsvp::text::Token token;
    token.text_utf8 = frame.text_utf8;
    token.source_byte_start = frame.source_byte_start;
    token.source_byte_length = frame.source_byte_length;
    token.punctuation_flags = frame.punctuation_flags;
    snapshot_.visible_token = token;

    snapshot_.chapter_count = chapters_.size();
    snapshot_.chapter_index = chapter_index_for_frame(snapshot_.frame_index);
    if (!chapters_.empty()) {
        const auto& chapter = chapters_[snapshot_.chapter_index];
        snapshot_.chapter_title = chapter.title;
        snapshot_.chapter_frame_count = chapter.frame_count;
        snapshot_.chapter_frame_index =
            snapshot_.frame_index >= chapter.first_frame_index
                ? snapshot_.frame_index - chapter.first_frame_index
                : std::size_t{0};
    }
}

void PlaybackEngine::update_remaining() {
    if (frames_.empty() || snapshot_.token_index >= frames_.size()) {
        snapshot_.estimated_remaining_ms = 0;
        snapshot_.estimated_chapter_remaining_ms = 0;
        return;
    }

    std::uint64_t total = 0;
    for (std::size_t i = snapshot_.token_index; i < frames_.size(); ++i) {
        total = saturating_add(total, duration_for_index(i));
    }
    snapshot_.estimated_remaining_ms = total;

    std::uint64_t chapter_total = 0;
    if (!chapters_.empty()) {
        const auto& chapter = chapters_[chapter_index_for_frame(snapshot_.frame_index)];
        const auto chapter_end =
            std::min(chapter.first_frame_index + chapter.frame_count, frames_.size());
        for (std::size_t i = snapshot_.token_index; i < chapter_end; ++i) {
            chapter_total = saturating_add(chapter_total, duration_for_index(i));
        }
    }
    snapshot_.estimated_chapter_remaining_ms = chapter_total;
}

void PlaybackEngine::schedule_next_from(std::uint64_t monotonic_ms) {
    next_due_ms_ = saturating_add(monotonic_ms, duration_for_index(snapshot_.token_index));
}

void PlaybackEngine::finish() {
    snapshot_.state = PlaybackState::finished;
    update_visible_token();
    snapshot_.estimated_remaining_ms = 0;
    snapshot_.estimated_chapter_remaining_ms = 0;
}

} // namespace rsvp::playback

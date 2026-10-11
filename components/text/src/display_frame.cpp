#include "rsvp/text/display_frame.hpp"

#include <algorithm>

namespace rsvp::text {
namespace {

struct Utf8Span {
    std::size_t byte_start = 0;
    std::size_t byte_length = 0;
};

std::size_t codepoint_length(std::string_view text, std::size_t offset) {
    const auto first = static_cast<unsigned char>(text[offset]);
    const auto remaining = text.size() - offset;

    if (first < 0x80U) {
        return 1;
    }
    if ((first & 0xE0U) == 0xC0U && remaining >= 2) {
        return 2;
    }
    if ((first & 0xF0U) == 0xE0U && remaining >= 3) {
        return 3;
    }
    if ((first & 0xF8U) == 0xF0U && remaining >= 4) {
        return 4;
    }
    return 1;
}

std::vector<Utf8Span> spans_for_text(std::string_view text) {
    std::vector<Utf8Span> spans;
    for (std::size_t offset = 0; offset < text.size();) {
        const auto length = codepoint_length(text, offset);
        spans.push_back(Utf8Span{offset, length});
        offset += length;
    }
    return spans;
}

PunctuationFlags flags_for_chunk(const Token& token,
                                 std::size_t chunk_index,
                                 std::size_t chunk_count) {
    return chunk_index + 1 == chunk_count ? token.punctuation_flags
                                          : PunctuationFlags::none;
}

std::size_t continuation_marker_codepoints(const DisplayFrameOptions& options) {
    return options.continuation_marker.empty()
               ? std::size_t{0}
               : utf8_codepoint_count(options.continuation_marker);
}

} // namespace

std::size_t utf8_codepoint_count(std::string_view text) {
    return spans_for_text(text).size();
}

std::size_t orp_index_for_codepoints(std::size_t codepoint_count) {
    if (codepoint_count <= 1) {
        return 0;
    }
    if (codepoint_count <= 5) {
        return 1;
    }
    if (codepoint_count <= 9) {
        return 2;
    }
    if (codepoint_count <= 13) {
        return 3;
    }
    return 4;
}

std::size_t centered_index_for_codepoints(std::size_t codepoint_count) {
    if (codepoint_count == 0) {
        return 0;
    }
    return (codepoint_count - 1) / 2;
}

int duration_percent_for_codepoints(std::size_t codepoint_count,
                                    bool continuation_before,
                                    bool continuation_after) {
    if (codepoint_count <= 2) {
        return 85;
    }
    if (codepoint_count <= 5) {
        return 100;
    }
    if (codepoint_count <= 8) {
        return 110;
    }
    if (codepoint_count <= 12) {
        return 120;
    }
    const int long_word_percent = continuation_before || continuation_after ? 150 : 135;
    return long_word_percent;
}

int font_scale_percent_for_codepoints(std::size_t codepoint_count,
                                      const DisplayFrameOptions& options) {
    const auto unscaled =
        std::max<std::size_t>(1, options.unscaled_codepoints_per_frame);
    const auto max_codepoints =
        std::max<std::size_t>(unscaled, options.max_codepoints_per_frame);
    const auto min_scale = std::clamp(options.min_font_scale_percent, 50, 100);

    if (codepoint_count <= unscaled) {
        return 100;
    }
    if (max_codepoints == unscaled) {
        return min_scale;
    }

    const auto extra = codepoint_count - unscaled;
    const auto span = max_codepoints - unscaled;
    const auto scale_drop =
        static_cast<std::size_t>(100 - min_scale) * extra + span - 1;
    const auto scale = 100 - static_cast<int>(scale_drop / span);
    return std::clamp(scale, min_scale, 100);
}

std::vector<DisplayFrame> make_display_frames(const std::vector<Token>& tokens,
                                              const DisplayFrameOptions& options) {
    std::vector<DisplayFrame> frames;
    const auto max_codepoints = std::max<std::size_t>(1, options.max_codepoints_per_frame);
    const auto marker_codepoints = continuation_marker_codepoints(options);
    const auto split_source_codepoints =
        marker_codepoints >= max_codepoints ? max_codepoints
                                            : max_codepoints - marker_codepoints;

    for (std::size_t token_index = 0; token_index < tokens.size(); ++token_index) {
        const auto& token = tokens[token_index];
        const auto spans = spans_for_text(token.text_utf8);
        const auto codepoints = spans.size();
        const bool split = options.chunk_long_tokens && codepoints > max_codepoints;
        const auto chunk_count =
            split ? (codepoints + split_source_codepoints - 1) / split_source_codepoints
                  : std::size_t{1};

        for (std::size_t chunk_index = 0; chunk_index < chunk_count; ++chunk_index) {
            const auto first_codepoint =
                split ? (codepoints * chunk_index) / chunk_count
                      : chunk_index * max_codepoints;
            const auto past_codepoint =
                split ? (codepoints * (chunk_index + 1)) / chunk_count
                      : codepoints;

            const auto source_byte_start =
                codepoints == 0 ? std::size_t{0} : spans[first_codepoint].byte_start;
            const auto source_byte_end =
                codepoints == 0
                    ? std::size_t{0}
                    : spans[past_codepoint - 1].byte_start +
                          spans[past_codepoint - 1].byte_length;
            const auto source_byte_length = source_byte_end - source_byte_start;

            DisplayFrame frame;
            frame.text_utf8 =
                token.text_utf8.substr(source_byte_start, source_byte_length);
            frame.source_token_index = token_index;
            frame.chunk_index = chunk_index;
            frame.chunk_count = chunk_count;
            frame.source_byte_start = token.source_byte_start + source_byte_start;
            frame.source_byte_length = source_byte_length;
            frame.continuation_before = chunk_index > 0;
            frame.continuation_after = chunk_index + 1 < chunk_count;
            if (frame.continuation_after && !options.continuation_marker.empty()) {
                frame.text_utf8 += options.continuation_marker;
            }
            frame.centered_fallback = split;
            frame.codepoint_count = utf8_codepoint_count(frame.text_utf8);
            frame.orp_codepoint_index =
                split ? centered_index_for_codepoints(frame.codepoint_count)
                      : orp_index_for_codepoints(codepoints);
            frame.duration_percent = duration_percent_for_codepoints(
                codepoints, frame.continuation_before, frame.continuation_after);
            frame.font_scale_percent =
                split ? font_scale_percent_for_codepoints(frame.codepoint_count, options)
                      : font_scale_percent_for_codepoints(codepoints, options);
            frame.punctuation_flags = flags_for_chunk(token, chunk_index, chunk_count);
            frames.push_back(frame);
        }
    }

    return frames;
}

} // namespace rsvp::text

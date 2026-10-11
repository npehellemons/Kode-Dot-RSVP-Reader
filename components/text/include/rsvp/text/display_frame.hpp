#pragma once

#include "rsvp/text/tokenizer.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace rsvp::text {

struct DisplayFrameOptions {
    std::size_t max_codepoints_per_frame = 16;
    std::size_t unscaled_codepoints_per_frame = 10;
    int min_font_scale_percent = 78;
    bool chunk_long_tokens = true;
    std::string continuation_marker = "-";
};

struct DisplayFrame {
    std::string text_utf8;
    std::size_t source_token_index = 0;
    std::size_t chunk_index = 0;
    std::size_t chunk_count = 1;
    std::size_t source_byte_start = 0;
    std::size_t source_byte_length = 0;
    std::size_t codepoint_count = 0;
    std::size_t orp_codepoint_index = 0;
    int duration_percent = 100;
    int font_scale_percent = 100;
    bool centered_fallback = false;
    bool continuation_before = false;
    bool continuation_after = false;
    PunctuationFlags punctuation_flags = PunctuationFlags::none;
};

std::size_t utf8_codepoint_count(std::string_view text);
std::size_t orp_index_for_codepoints(std::size_t codepoint_count);
std::size_t centered_index_for_codepoints(std::size_t codepoint_count);
int duration_percent_for_codepoints(std::size_t codepoint_count,
                                    bool continuation_before,
                                    bool continuation_after);
int font_scale_percent_for_codepoints(std::size_t codepoint_count,
                                      const DisplayFrameOptions& options = {});

std::vector<DisplayFrame> make_display_frames(
    const std::vector<Token>& tokens,
    const DisplayFrameOptions& options = {});

} // namespace rsvp::text

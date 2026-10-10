#pragma once

#include "rsvp/text/display_frame.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace rsvp::text {

struct Chapter {
    std::string title;
    std::size_t source_byte_start = 0;
    std::size_t source_byte_length = 0;
    std::size_t first_frame_index = 0;
    std::size_t frame_count = 0;
};

std::vector<Chapter> make_single_chapter(
    const std::vector<DisplayFrame>& frames,
    std::string title = "Document");

std::vector<Chapter> make_chapters_from_markdown_headings(
    std::string_view source,
    const std::vector<DisplayFrame>& frames,
    std::string fallback_title = "Document");

} // namespace rsvp::text

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

struct SourceChapter {
    std::string title;
    std::size_t source_byte_start = 0;
    std::size_t source_byte_length = 0;
};

std::vector<SourceChapter> make_single_source_chapter(
    std::string_view source,
    std::string title = "Document");

std::vector<SourceChapter> make_source_chapters_from_headings(
    std::string_view source,
    std::string fallback_title = "Document");

std::vector<Chapter> make_frame_chapters(
    const std::vector<SourceChapter>& source_chapters,
    const std::vector<DisplayFrame>& frames);

std::vector<Chapter> make_single_chapter(
    const std::vector<DisplayFrame>& frames,
    std::string title = "Document");

std::vector<Chapter> make_chapters_from_markdown_headings(
    std::string_view source,
    const std::vector<DisplayFrame>& frames,
    std::string fallback_title = "Document");

} // namespace rsvp::text

#include "rsvp/text/chapter.hpp"

#include <algorithm>

namespace rsvp::text {
namespace {

struct Heading {
    std::string title;
    std::size_t byte_start = 0;
    bool skip_front_matter = false;
};

std::string trim_heading_title(std::string_view line) {
    std::size_t first = 0;
    while (first < line.size() && line[first] == '#') {
        ++first;
    }
    while (first < line.size() && (line[first] == ' ' || line[first] == '\t')) {
        ++first;
    }

    std::size_t last = line.size();
    while (last > first && (line[last - 1] == ' ' || line[last - 1] == '\t' ||
                           line[last - 1] == '\r')) {
        --last;
    }
    return std::string(line.substr(first, last - first));
}

bool is_heading_line(std::string_view line) {
    if (line.empty() || line.front() != '#') {
        return false;
    }
    std::size_t hashes = 0;
    while (hashes < line.size() && line[hashes] == '#') {
        ++hashes;
    }
    return hashes > 0 && hashes <= 6 && hashes < line.size() &&
           (line[hashes] == ' ' || line[hashes] == '\t');
}

bool is_roman_numeral(char ch) {
    return ch == 'I' || ch == 'V' || ch == 'X' || ch == 'L' ||
           ch == 'C' || ch == 'D' || ch == 'M';
}

bool has_lowercase_ascii(std::string_view value) {
    for (const char ch : value) {
        if (ch >= 'a' && ch <= 'z') {
            return true;
        }
    }
    return false;
}

std::string trim_ascii(std::string_view value) {
    std::size_t first = 0;
    while (first < value.size() && (value[first] == ' ' || value[first] == '\t' ||
                                   value[first] == '\r')) {
        ++first;
    }

    std::size_t last = value.size();
    while (last > first && (value[last - 1] == ' ' || value[last - 1] == '\t' ||
                           value[last - 1] == '\r')) {
        --last;
    }
    return std::string(value.substr(first, last - first));
}

bool is_gutenberg_chapter_line(std::string_view line) {
    if (line.empty() || line.front() == ' ' || line.front() == '\t') {
        return false;
    }

    std::size_t index = 0;
    while (index < line.size() && is_roman_numeral(line[index])) {
        ++index;
    }
    if (index == 0 || index >= line.size() || line[index] != '.') {
        return false;
    }

    ++index;
    if (index >= line.size() || (line[index] != ' ' && line[index] != '\t')) {
        return false;
    }

    const auto title = trim_ascii(line.substr(index));
    return !title.empty() && title.size() <= 96 && !has_lowercase_ascii(title);
}

std::string title_from_heading_line(std::string_view line) {
    if (is_heading_line(line)) {
        return trim_heading_title(line);
    }
    return trim_ascii(line);
}

std::vector<Heading> headings_from_source(std::string_view source) {
    std::vector<Heading> headings;
    std::size_t line_start = 0;
    while (line_start <= source.size()) {
        const auto line_end = source.find('\n', line_start);
        const auto length = line_end == std::string_view::npos
                                ? source.size() - line_start
                                : line_end - line_start;
        const auto line = source.substr(line_start, length);
        const bool markdown_heading = is_heading_line(line);
        const bool gutenberg_heading = is_gutenberg_chapter_line(line);
        if (markdown_heading || gutenberg_heading) {
            auto title = title_from_heading_line(line);
            if (!title.empty()) {
                headings.push_back({std::move(title), line_start, gutenberg_heading});
            }
        }

        if (line_end == std::string_view::npos) {
            break;
        }
        line_start = line_end + 1;
    }
    return headings;
}

std::size_t first_frame_at_or_after(const std::vector<DisplayFrame>& frames,
                                    std::size_t source_byte_offset) {
    for (std::size_t index = 0; index < frames.size(); ++index) {
        if (frames[index].source_byte_start >= source_byte_offset) {
            return index;
        }
    }
    return frames.size();
}

} // namespace

std::vector<Chapter> make_single_chapter(
    const std::vector<DisplayFrame>& frames,
    std::string title) {
    Chapter chapter;
    chapter.title = std::move(title);
    chapter.first_frame_index = 0;
    chapter.frame_count = frames.size();
    if (!frames.empty()) {
        chapter.source_byte_start = frames.front().source_byte_start;
        const auto& last = frames.back();
        chapter.source_byte_length =
            last.source_byte_start + last.source_byte_length - chapter.source_byte_start;
    }
    return {chapter};
}

std::vector<Chapter> make_chapters_from_markdown_headings(
    std::string_view source,
    const std::vector<DisplayFrame>& frames,
    std::string fallback_title) {
    auto headings = headings_from_source(source);
    if (headings.empty()) {
        return make_single_chapter(frames, std::move(fallback_title));
    }

    if (headings.front().byte_start > 0 && !headings.front().skip_front_matter) {
        headings.insert(headings.begin(), {std::move(fallback_title), 0, false});
    }

    std::vector<Chapter> chapters;
    chapters.reserve(headings.size());
    for (std::size_t index = 0; index < headings.size(); ++index) {
        const auto source_start = headings[index].byte_start;
        const auto source_end =
            index + 1 < headings.size() ? headings[index + 1].byte_start : source.size();
        const auto first_frame = first_frame_at_or_after(frames, source_start);
        const auto next_frame = first_frame_at_or_after(frames, source_end);
        if (first_frame >= next_frame) {
            continue;
        }

        Chapter chapter;
        chapter.title = headings[index].title;
        chapter.source_byte_start = source_start;
        chapter.source_byte_length = source_end - source_start;
        chapter.first_frame_index = first_frame;
        chapter.frame_count = next_frame - first_frame;
        chapters.push_back(std::move(chapter));
    }

    if (chapters.empty()) {
        return make_single_chapter(frames, std::move(fallback_title));
    }
    return chapters;
}

} // namespace rsvp::text

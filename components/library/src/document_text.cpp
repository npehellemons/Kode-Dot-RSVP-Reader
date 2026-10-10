#include "rsvp/library/document_text.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <utility>
#include <vector>

namespace rsvp::library {
namespace {

std::string trim_ascii(std::string_view value) {
    std::size_t first = 0;
    while (first < value.size() &&
           std::isspace(static_cast<unsigned char>(value[first])) != 0) {
        ++first;
    }

    std::size_t last = value.size();
    while (last > first &&
           std::isspace(static_cast<unsigned char>(value[last - 1])) != 0) {
        --last;
    }
    return std::string(value.substr(first, last - first));
}

std::string basename_without_extension(std::string_view filename) {
    auto end = filename.size();
    while (end > 0 && (filename[end - 1] == '/' || filename[end - 1] == '\\')) {
        --end;
    }

    auto start = filename.substr(0, end).find_last_of("/\\");
    start = start == std::string_view::npos ? 0 : start + 1;
    const auto base = filename.substr(start, end - start);
    const auto dot = base.find_last_of('.');
    if (dot == std::string_view::npos || dot == 0) {
        return trim_ascii(base);
    }
    return trim_ascii(base.substr(0, dot));
}

void apply_filename_metadata(
    DocumentText& document,
    std::string_view filename,
    std::string_view fallback_title) {
    const auto base = basename_without_extension(filename);
    if (base.empty()) {
        document.title = std::string(fallback_title);
        document.metadata_confidence = MetadataConfidence::unknown;
        return;
    }

    const auto separator = base.find(" - ");
    if (separator != std::string::npos) {
        document.author = trim_ascii(std::string_view(base).substr(0, separator));
        document.title = trim_ascii(std::string_view(base).substr(separator + 3));
        document.metadata_confidence =
            document.title.empty() ? MetadataConfidence::filename_only
                                   : MetadataConfidence::filename_convention;
        if (document.title.empty()) {
            document.title = base;
        }
        return;
    }

    document.title = base;
    document.metadata_confidence = MetadataConfidence::filename_only;
}

bool starts_with_ascii_case_insensitive(std::string_view value,
                                        std::string_view prefix) {
    if (value.size() < prefix.size()) {
        return false;
    }
    for (std::size_t index = 0; index < prefix.size(); ++index) {
        const auto lhs = static_cast<unsigned char>(value[index]);
        const auto rhs = static_cast<unsigned char>(prefix[index]);
        if (std::tolower(lhs) != std::tolower(rhs)) {
            return false;
        }
    }
    return true;
}

void apply_explicit_text_metadata(DocumentText& document, std::string_view source) {
    std::size_t line_start = 0;
    std::size_t lines_seen = 0;
    while (line_start <= source.size() && lines_seen < 200) {
        const auto line_end = source.find('\n', line_start);
        const auto length = line_end == std::string_view::npos
                                ? source.size() - line_start
                                : line_end - line_start;
        const auto line = source.substr(line_start, length);
        if (starts_with_ascii_case_insensitive(line, "Title:")) {
            const auto title = trim_ascii(line.substr(6));
            if (!title.empty()) {
                document.title = title;
                document.metadata_confidence = MetadataConfidence::explicit_metadata;
            }
        } else if (starts_with_ascii_case_insensitive(line, "Author:")) {
            const auto author = trim_ascii(line.substr(7));
            if (!author.empty()) {
                document.author = author;
                document.metadata_confidence = MetadataConfidence::explicit_metadata;
            }
        }

        if (line_end == std::string_view::npos) {
            break;
        }
        line_start = line_end + 1;
        ++lines_seen;
    }
}

void add_metadata_ranges(
    std::vector<std::pair<std::size_t, std::size_t>>& ranges,
    std::string_view source) {
    std::size_t line_start = 0;
    std::size_t lines_seen = 0;
    while (line_start <= source.size() && lines_seen < 200) {
        const auto line_end = source.find('\n', line_start);
        const auto after_line =
            line_end == std::string_view::npos ? source.size() : line_end + 1;
        const auto line = source.substr(line_start, after_line - line_start);
        if (starts_with_ascii_case_insensitive(line, "Title:") ||
            starts_with_ascii_case_insensitive(line, "Author:")) {
            ranges.push_back({line_start, after_line});
        }

        if (line_end == std::string_view::npos) {
            break;
        }
        line_start = after_line;
        ++lines_seen;
    }
}

bool is_word_byte(char ch) {
    return std::isspace(static_cast<unsigned char>(ch)) == 0;
}

std::size_t count_words(std::string_view text) {
    std::size_t words = 0;
    bool inside_word = false;
    for (const char ch : text) {
        if (is_word_byte(ch)) {
            if (!inside_word) {
                ++words;
                inside_word = true;
            }
        } else {
            inside_word = false;
        }
    }
    return words;
}

std::vector<rsvp::text::SourceChapter> make_auto_chunks(
    std::string_view source,
    std::size_t target_chunk_words,
    std::string_view fallback_title) {
    if (source.empty()) {
        return rsvp::text::make_single_source_chapter(source, std::string(fallback_title));
    }

    const auto word_count = count_words(source);
    if (target_chunk_words == 0 || word_count <= target_chunk_words) {
        return rsvp::text::make_single_source_chapter(source, std::string(fallback_title));
    }

    std::vector<rsvp::text::SourceChapter> chapters;
    std::size_t chunk_start = 0;
    std::size_t line_start = 0;
    std::size_t words_in_chunk = 0;
    std::size_t part = 1;

    while (line_start <= source.size()) {
        const auto line_end = source.find('\n', line_start);
        const auto after_line =
            line_end == std::string_view::npos ? source.size() : line_end + 1;
        const auto line = source.substr(line_start, after_line - line_start);
        words_in_chunk += count_words(line);

        if (words_in_chunk >= target_chunk_words && after_line > chunk_start) {
            rsvp::text::SourceChapter chapter;
            chapter.title = "Part " + std::to_string(part++);
            chapter.source_byte_start = chunk_start;
            chapter.source_byte_length = after_line - chunk_start;
            chapters.push_back(std::move(chapter));
            chunk_start = after_line;
            words_in_chunk = 0;
        }

        if (line_end == std::string_view::npos) {
            break;
        }
        line_start = after_line;
    }

    if (chunk_start < source.size()) {
        rsvp::text::SourceChapter chapter;
        chapter.title = "Part " + std::to_string(part);
        chapter.source_byte_start = chunk_start;
        chapter.source_byte_length = source.size() - chunk_start;
        chapters.push_back(std::move(chapter));
    }

    if (chapters.size() <= 1) {
        return rsvp::text::make_single_source_chapter(source, std::string(fallback_title));
    }
    return chapters;
}

bool is_metadata_front_matter(
    std::string_view source,
    const rsvp::text::SourceChapter& chapter) {
    const auto start = std::min(chapter.source_byte_start, source.size());
    const auto length = std::min(chapter.source_byte_length, source.size() - start);
    const auto text = source.substr(start, length);
    return text.find("Title:") != std::string_view::npos ||
           text.find("Author:") != std::string_view::npos;
}

void add_heading_ranges(
    std::vector<std::pair<std::size_t, std::size_t>>& ranges,
    std::string_view source,
    const std::vector<rsvp::text::SourceChapter>& chapters) {
    for (const auto& chapter : chapters) {
        const auto start = std::min(chapter.source_byte_start, source.size());
        const auto line_end = source.find('\n', start);
        const auto after_line =
            line_end == std::string_view::npos ? source.size() : line_end + 1;
        if (after_line > start) {
            ranges.push_back({start, after_line});
        }
    }
}

std::string make_reading_text(
    std::string_view source,
    const std::vector<std::pair<std::size_t, std::size_t>>& ranges) {
    std::string text(source);
    for (const auto& range : ranges) {
        const auto start = std::min(range.first, text.size());
        const auto end = std::min(range.second, text.size());
        for (std::size_t index = start; index < end; ++index) {
            if (text[index] != '\n' && text[index] != '\r') {
                text[index] = ' ';
            }
        }
    }
    return text;
}

} // namespace

DocumentText import_txt_document(
    std::string_view source,
    std::string_view filename,
    const TxtImportOptions& options) {
    DocumentText document;
    document.format = DocumentFormat::txt;
    document.normalized_text_utf8 = std::string(source);
    std::vector<std::pair<std::size_t, std::size_t>> reading_exclusions;
    add_metadata_ranges(reading_exclusions, source);

    apply_filename_metadata(document, filename, options.fallback_title);
    apply_explicit_text_metadata(document, source);
    if (document.title.empty()) {
        document.title = options.fallback_title;
    }

    auto source_chapters =
        rsvp::text::make_source_chapters_from_headings(source, document.title);
    if (source_chapters.size() > 1 &&
        source_chapters.front().title == document.title &&
        is_metadata_front_matter(source, source_chapters.front())) {
        source_chapters.erase(source_chapters.begin());
    }
    if (source_chapters.size() > 1 ||
        (source_chapters.size() == 1 && source_chapters.front().title != document.title)) {
        document.chapter_source = ChapterSource::explicit_headings;
        if (!source_chapters.empty() && source_chapters.front().source_byte_start > 0) {
            reading_exclusions.push_back({0, source_chapters.front().source_byte_start});
        }
        add_heading_ranges(reading_exclusions, source, source_chapters);
        document.reading_text_utf8 = make_reading_text(source, reading_exclusions);
        document.chapters = std::move(source_chapters);
        return document;
    }

    auto chunks = make_auto_chunks(source, options.target_chunk_words, document.title);
    document.chapter_source =
        chunks.size() > 1 ? ChapterSource::auto_chunked : ChapterSource::single_fallback;
    document.reading_text_utf8 = make_reading_text(source, reading_exclusions);
    document.chapters = std::move(chunks);
    return document;
}

std::vector<rsvp::text::Chapter> make_frame_chapters(
    const DocumentText& document,
    const std::vector<rsvp::text::DisplayFrame>& frames) {
    return rsvp::text::make_frame_chapters(document.chapters, frames);
}

} // namespace rsvp::library

#pragma once

#include "rsvp/text/chapter.hpp"
#include "rsvp/text/display_frame.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace rsvp::library {

enum class DocumentFormat {
    txt,
    epub,
};

enum class MetadataConfidence {
    explicit_metadata,
    filename_convention,
    filename_only,
    unknown,
};

enum class ChapterSource {
    explicit_headings,
    auto_chunked,
    single_fallback,
};

struct DocumentText {
    std::string title;
    std::string author;
    DocumentFormat format = DocumentFormat::txt;
    MetadataConfidence metadata_confidence = MetadataConfidence::unknown;
    ChapterSource chapter_source = ChapterSource::single_fallback;
    std::string normalized_text_utf8;
    std::string reading_text_utf8;
    std::vector<rsvp::text::SourceChapter> chapters;
};

struct TxtImportOptions {
    std::size_t target_chunk_words = 3000;
    std::string fallback_title = "Document";
};

DocumentText import_txt_document(
    std::string_view source,
    std::string_view filename = {},
    const TxtImportOptions& options = {});

std::vector<rsvp::text::Chapter> make_frame_chapters(
    const DocumentText& document,
    const std::vector<rsvp::text::DisplayFrame>& frames);

} // namespace rsvp::library

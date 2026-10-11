#pragma once

#include "rsvp/library/document_text.hpp"
#include "rsvp/text/chapter.hpp"
#include "rsvp/text/display_frame.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rsvp::library {

struct BookRecord {
    std::string id;
    std::string title;
    std::string author;
    DocumentFormat format = DocumentFormat::txt;
    MetadataConfidence metadata_confidence = MetadataConfidence::unknown;
    ChapterSource chapter_source = ChapterSource::single_fallback;
    std::string source_path;
    std::string folder;
    std::uintmax_t file_size_bytes = 0;
    std::uint64_t content_hash = 0;
    std::size_t chapter_count = 0;
    std::size_t frame_count = 0;
    std::size_t resume_frame_index = 0;
    std::size_t resume_chapter_index = 0;
};

struct LibraryCatalog {
    std::vector<BookRecord> books;
};

std::string make_book_id(const DocumentText& document,
                         std::uint64_t content_hash);

BookRecord make_book_record(
    const DocumentText& document,
    const std::vector<rsvp::text::DisplayFrame>& frames,
    const std::vector<rsvp::text::Chapter>& chapters,
    std::uint64_t content_hash,
    std::string source_path = {},
    std::string folder = {},
    std::string id = {},
    std::uintmax_t file_size_bytes = 0);

BookRecord make_book_record_from_file_metadata(
    DocumentFormat format,
    std::string source_path,
    std::uintmax_t file_size_bytes,
    std::uint64_t content_hash,
    std::string id = {},
    std::string title = {},
    std::string folder = {});

std::size_t chapter_index_for_frame(
    const std::vector<rsvp::text::Chapter>& chapters,
    std::size_t frame_index);

void update_resume_position(
    BookRecord& book,
    std::size_t frame_index,
    const std::vector<rsvp::text::Chapter>& chapters);

std::optional<std::size_t> find_book_index(
    const LibraryCatalog& catalog,
    std::string_view book_id);

void upsert_book(LibraryCatalog& catalog, BookRecord book);

std::vector<std::string> folder_names(const LibraryCatalog& catalog);

std::vector<const BookRecord*> books_in_folder(
    const LibraryCatalog& catalog,
    std::string_view folder);

} // namespace rsvp::library

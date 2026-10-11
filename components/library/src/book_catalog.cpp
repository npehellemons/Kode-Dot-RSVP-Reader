#include "rsvp/library/book_catalog.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <utility>

namespace rsvp::library {
namespace {

std::string to_hex(std::uint64_t value) {
    std::ostringstream stream;
    stream << std::hex << std::setfill('0') << std::setw(16) << value;
    return stream.str();
}

std::string slug_part(std::string_view value) {
    std::string slug;
    bool previous_dash = false;
    for (const unsigned char ch : value) {
        if (std::isalnum(ch) != 0) {
            slug.push_back(static_cast<char>(std::tolower(ch)));
            previous_dash = false;
        } else if (!previous_dash && !slug.empty()) {
            slug.push_back('-');
            previous_dash = true;
        }
    }
    while (!slug.empty() && slug.back() == '-') {
        slug.pop_back();
    }
    return slug;
}

std::string display_title(const DocumentText& document) {
    return document.title.empty() ? "Document" : document.title;
}

std::string title_from_source_path(std::string_view source_path) {
    const auto path = std::filesystem::path(std::string(source_path));
    auto title = path.stem().string();
    return title.empty() ? "Document" : title;
}

std::size_t clamped_frame_index(std::size_t frame_index, std::size_t frame_count) {
    if (frame_count == 0) {
        return 0;
    }
    return std::min(frame_index, frame_count - 1);
}

} // namespace

std::string make_book_id(const DocumentText& document,
                         std::uint64_t content_hash) {
    auto base = slug_part(display_title(document));
    const auto author = slug_part(document.author);
    if (!author.empty()) {
        base = author + "-" + base;
    }
    if (base.empty()) {
        base = "document";
    }
    return base + "-" + to_hex(content_hash);
}

BookRecord make_book_record(
    const DocumentText& document,
    const std::vector<rsvp::text::DisplayFrame>& frames,
    const std::vector<rsvp::text::Chapter>& chapters,
    std::uint64_t content_hash,
    std::string source_path,
    std::string folder,
    std::string id,
    std::uintmax_t file_size_bytes) {
    BookRecord book;
    book.id = id.empty() ? make_book_id(document, content_hash) : std::move(id);
    book.title = display_title(document);
    book.author = document.author;
    book.format = document.format;
    book.metadata_confidence = document.metadata_confidence;
    book.chapter_source = document.chapter_source;
    book.source_path = std::move(source_path);
    book.folder = std::move(folder);
    book.file_size_bytes = file_size_bytes;
    book.content_hash = content_hash;
    book.chapter_count = chapters.empty() ? document.chapters.size() : chapters.size();
    book.frame_count = frames.size();
    book.resume_frame_index = 0;
    book.resume_chapter_index = chapter_index_for_frame(chapters, 0);
    return book;
}

BookRecord make_book_record_from_file_metadata(
    DocumentFormat format,
    std::string source_path,
    std::uintmax_t file_size_bytes,
    std::uint64_t content_hash,
    std::string id,
    std::string title,
    std::string folder) {
    BookRecord book;
    book.format = format;
    book.title = title.empty() ? title_from_source_path(source_path) : std::move(title);
    book.id = id.empty() ? slug_part(book.title) + "-" + to_hex(content_hash)
                         : std::move(id);
    book.metadata_confidence = MetadataConfidence::filename_only;
    book.chapter_source = ChapterSource::single_fallback;
    book.source_path = std::move(source_path);
    book.folder = std::move(folder);
    book.file_size_bytes = file_size_bytes;
    book.content_hash = content_hash;
    return book;
}

std::size_t chapter_index_for_frame(
    const std::vector<rsvp::text::Chapter>& chapters,
    std::size_t frame_index) {
    if (chapters.empty()) {
        return 0;
    }

    for (std::size_t index = 0; index < chapters.size(); ++index) {
        const auto& chapter = chapters[index];
        const auto chapter_end = chapter.first_frame_index + chapter.frame_count;
        if (chapter.frame_count == 0) {
            continue;
        }
        if (frame_index >= chapter.first_frame_index && frame_index < chapter_end) {
            return index;
        }
    }

    if (frame_index < chapters.front().first_frame_index) {
        return 0;
    }
    return chapters.size() - 1;
}

void update_resume_position(
    BookRecord& book,
    std::size_t frame_index,
    const std::vector<rsvp::text::Chapter>& chapters) {
    book.resume_frame_index = clamped_frame_index(frame_index, book.frame_count);
    book.resume_chapter_index =
        chapter_index_for_frame(chapters, book.resume_frame_index);
}

std::optional<std::size_t> find_book_index(
    const LibraryCatalog& catalog,
    std::string_view book_id) {
    for (std::size_t index = 0; index < catalog.books.size(); ++index) {
        if (catalog.books[index].id == book_id) {
            return index;
        }
    }
    return std::nullopt;
}

void upsert_book(LibraryCatalog& catalog, BookRecord book) {
    const auto existing = find_book_index(catalog, book.id);
    if (existing) {
        catalog.books[*existing] = std::move(book);
        return;
    }
    catalog.books.push_back(std::move(book));
}

std::vector<std::string> folder_names(const LibraryCatalog& catalog) {
    std::vector<std::string> folders;
    for (const auto& book : catalog.books) {
        const auto name = book.folder.empty() ? "Library" : book.folder;
        if (std::find(folders.begin(), folders.end(), name) == folders.end()) {
            folders.push_back(name);
        }
    }
    std::sort(folders.begin(), folders.end());
    return folders;
}

std::vector<const BookRecord*> books_in_folder(
    const LibraryCatalog& catalog,
    std::string_view folder) {
    std::vector<const BookRecord*> books;
    const auto requested = folder.empty() ? std::string_view("Library") : folder;
    for (const auto& book : catalog.books) {
        const auto name = book.folder.empty() ? std::string_view("Library")
                                             : std::string_view(book.folder);
        if (name == requested) {
            books.push_back(&book);
        }
    }
    return books;
}

} // namespace rsvp::library

#include "rsvp/storage/book_storage.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <system_error>
#include <utility>

namespace rsvp::storage {
namespace {

std::string lowercase_extension(const std::filesystem::path& path) {
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char ch) {
                       return static_cast<char>(std::tolower(ch));
                   });
    return extension;
}

std::filesystem::path existing_space_target(std::filesystem::path path) {
    std::error_code error;
    while (!path.empty() && !std::filesystem::exists(path, error)) {
        path = path.parent_path();
        error.clear();
    }
    return path.empty() ? std::filesystem::current_path(error) : path;
}

std::string book_id_from_path(const std::filesystem::path& path) {
    return sanitize_storage_key(path.stem().string());
}

} // namespace

BookStorageLayout make_book_storage_layout(const std::filesystem::path& root) {
    BookStorageLayout layout;
    layout.root = root;
    layout.books_dir = root / "books";
    layout.state_dir = root / "state";
    layout.resume_dir = layout.state_dir / "resume";
    layout.catalog_path = layout.state_dir / "catalog.state";
    layout.settings_path = layout.state_dir / "settings.state";
    return layout;
}

std::string sanitize_storage_key(std::string_view value) {
    std::string sanitized;
    bool previous_dash = false;
    for (const unsigned char ch : value) {
        const bool allowed = std::isalnum(ch) != 0 || ch == '_' || ch == '-' || ch == '.';
        if (allowed) {
            sanitized.push_back(static_cast<char>(ch));
            previous_dash = false;
        } else if (!previous_dash && !sanitized.empty()) {
            sanitized.push_back('-');
            previous_dash = true;
        }
    }

    while (!sanitized.empty() &&
           (sanitized.front() == '.' || sanitized.front() == '-')) {
        sanitized.erase(sanitized.begin());
    }
    while (!sanitized.empty() &&
           (sanitized.back() == '.' || sanitized.back() == '-')) {
        sanitized.pop_back();
    }
    return sanitized.empty() ? "book" : sanitized;
}

std::filesystem::path resume_path_for_book(
    const BookStorageLayout& layout,
    std::string_view book_id) {
    return layout.resume_dir / (sanitize_storage_key(book_id) + ".state");
}

std::filesystem::path imported_book_path(
    const BookStorageLayout& layout,
    std::string_view book_id,
    StoredBookFormat format) {
    return layout.books_dir /
           (sanitize_storage_key(book_id) + extension_for_stored_book_format(format));
}

std::optional<StoredBookFormat> stored_book_format_for_path(
    const std::filesystem::path& path) {
    const auto extension = lowercase_extension(path);
    if (extension == ".txt") {
        return StoredBookFormat::txt;
    }
    if (extension == ".epub") {
        return StoredBookFormat::epub;
    }
    return std::nullopt;
}

std::string extension_for_stored_book_format(StoredBookFormat format) {
    switch (format) {
    case StoredBookFormat::txt:
        return ".txt";
    case StoredBookFormat::epub:
        return ".epub";
    }
    return ".txt";
}

StorageStats make_storage_stats(
    const std::filesystem::path& root,
    std::size_t book_count,
    std::uintmax_t book_bytes) {
    StorageStats stats;
    stats.book_count = book_count;
    stats.book_bytes = book_bytes;

    std::error_code error;
    const auto target = existing_space_target(root);
    const auto space = std::filesystem::space(target, error);
    if (error) {
        stats.status = "Storage unavailable";
        return stats;
    }

    stats.ready = true;
    stats.capacity_bytes = space.capacity;
    stats.free_bytes = space.free;
    stats.available_bytes = space.available;
    stats.used_bytes =
        space.capacity >= space.free ? space.capacity - space.free : 0;
    stats.status = "Storage ready";
    return stats;
}

std::vector<StoredBookFile> list_stored_book_files(
    const BookStorageLayout& layout) {
    std::vector<StoredBookFile> books;
    std::error_code error;
    if (!std::filesystem::exists(layout.books_dir, error) ||
        !std::filesystem::is_directory(layout.books_dir, error)) {
        return books;
    }

    for (const auto& entry :
         std::filesystem::directory_iterator(layout.books_dir, error)) {
        if (error) {
            break;
        }
        if (!entry.is_regular_file(error)) {
            error.clear();
            continue;
        }

        const auto format = stored_book_format_for_path(entry.path());
        if (!format) {
            continue;
        }

        StoredBookFile book;
        book.id = book_id_from_path(entry.path());
        book.format = *format;
        book.source_path = entry.path();
        book.file_size_bytes = entry.file_size(error);
        if (error) {
            error.clear();
            book.file_size_bytes = 0;
        }
        books.push_back(std::move(book));
    }

    std::sort(books.begin(), books.end(),
              [](const StoredBookFile& lhs, const StoredBookFile& rhs) {
                  return lhs.id < rhs.id;
              });
    return books;
}

std::optional<std::string> load_stored_book_bytes(
    const StoredBookFile& book) {
    if (!stored_book_format_for_path(book.source_path)) {
        return std::nullopt;
    }

    std::ifstream stream(book.source_path, std::ios::binary);
    if (!stream) {
        return std::nullopt;
    }

    std::ostringstream buffer;
    buffer << stream.rdbuf();
    if (!stream.good() && !stream.eof()) {
        return std::nullopt;
    }
    return buffer.str();
}

StorageStats storage_stats_for_layout(const BookStorageLayout& layout) {
    const auto books = list_stored_book_files(layout);
    std::uintmax_t book_bytes = 0;
    for (const auto& book : books) {
        book_bytes += book.file_size_bytes;
    }
    return make_storage_stats(layout.root, books.size(), book_bytes);
}

} // namespace rsvp::storage

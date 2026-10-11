#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace rsvp::storage {

enum class StoredBookFormat {
    txt,
    epub,
};

struct BookStorageLayout {
    std::filesystem::path root;
    std::filesystem::path books_dir;
    std::filesystem::path state_dir;
    std::filesystem::path resume_dir;
    std::filesystem::path catalog_path;
    std::filesystem::path settings_path;
};

struct StoredBookFile {
    std::string id;
    StoredBookFormat format = StoredBookFormat::txt;
    std::filesystem::path source_path;
    std::uintmax_t file_size_bytes = 0;
};

struct StorageStats {
    bool ready = false;
    std::uintmax_t capacity_bytes = 0;
    std::uintmax_t free_bytes = 0;
    std::uintmax_t available_bytes = 0;
    std::uintmax_t used_bytes = 0;
    std::size_t book_count = 0;
    std::uintmax_t book_bytes = 0;
    std::string status;
};

BookStorageLayout make_book_storage_layout(const std::filesystem::path& root);

std::string sanitize_storage_key(std::string_view value);

std::filesystem::path resume_path_for_book(
    const BookStorageLayout& layout,
    std::string_view book_id);

std::filesystem::path imported_book_path(
    const BookStorageLayout& layout,
    std::string_view book_id,
    StoredBookFormat format);

std::optional<StoredBookFormat> stored_book_format_for_path(
    const std::filesystem::path& path);

std::string extension_for_stored_book_format(StoredBookFormat format);

StorageStats make_storage_stats(
    const std::filesystem::path& root,
    std::size_t book_count,
    std::uintmax_t book_bytes);

} // namespace rsvp::storage

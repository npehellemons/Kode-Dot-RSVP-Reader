#include "kode_dot/reader_storage.hpp"

#include "rsvp/storage/book_storage.hpp"

#include <utility>

namespace kode_dot {

DirectoryReaderStorageProvider::DirectoryReaderStorageProvider(
    std::filesystem::path root)
    : root_(std::move(root)) {}

const std::filesystem::path& DirectoryReaderStorageProvider::root() const {
    return root_;
}

rsvp::lifecycle::ReaderStatePaths
DirectoryReaderStorageProvider::reader_state_paths() const {
    return reader_state_paths_for_directory(root_);
}

rsvp::lifecycle::ReaderStatePaths reader_state_paths_for_directory(
    const std::filesystem::path& root) {
    const auto layout = rsvp::storage::make_book_storage_layout(root);
    return rsvp::lifecycle::ReaderStatePaths{
        layout.settings_path,
        layout.state_dir / "bookmark.state",
        layout.resume_dir,
    };
}

} // namespace kode_dot

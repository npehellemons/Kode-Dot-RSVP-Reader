#include "kode_dot/reader_storage.hpp"

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
    return rsvp::lifecycle::ReaderStatePaths{
        root / "settings.txt",
        root / "bookmark.txt",
    };
}

} // namespace kode_dot

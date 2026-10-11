#pragma once

#include "rsvp/lifecycle/reader_lifecycle.hpp"

#include <filesystem>

namespace kode_dot {

class ReaderStorageProvider {
public:
    virtual ~ReaderStorageProvider() = default;
    virtual rsvp::lifecycle::ReaderStatePaths reader_state_paths() const = 0;
};

class DirectoryReaderStorageProvider : public ReaderStorageProvider {
public:
    explicit DirectoryReaderStorageProvider(std::filesystem::path root);

    const std::filesystem::path& root() const;
    rsvp::lifecycle::ReaderStatePaths reader_state_paths() const override;

private:
    std::filesystem::path root_;
};

rsvp::lifecycle::ReaderStatePaths reader_state_paths_for_directory(
    const std::filesystem::path& root);

} // namespace kode_dot

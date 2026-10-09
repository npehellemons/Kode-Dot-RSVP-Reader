#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace rsvp::storage {

enum class FixationMode {
    center,
    orp,
};

enum class PunctuationMode {
    off,
    gentle,
};

struct SettingsV1 {
    static constexpr int kSchemaVersion = 1;

    int schema_version = kSchemaVersion;
    int wpm = 250;
    int font_scale = 100;
    FixationMode fixation_mode = FixationMode::center;
    PunctuationMode punctuation_mode = PunctuationMode::gentle;
};

struct BookmarkV1 {
    static constexpr int kSchemaVersion = 1;

    int schema_version = kSchemaVersion;
    std::string doc_id;
    std::uint64_t content_hash = 0;
    std::size_t source_byte_offset = 0;
    std::size_t token_index = 0;
    std::uint64_t updated_at = 0;
};

std::uint64_t content_hash(std::string_view source);
std::string to_hex(std::uint64_t value);
std::optional<std::uint64_t> parse_hex_u64(std::string_view value);

std::string serialize_settings(const SettingsV1& settings);
std::optional<SettingsV1> parse_settings(std::string_view serialized);

std::string serialize_bookmark(const BookmarkV1& bookmark);
std::optional<BookmarkV1> parse_bookmark(std::string_view serialized);

bool save_settings_atomic(const std::filesystem::path& path, const SettingsV1& settings);
std::optional<SettingsV1> load_settings(const std::filesystem::path& path);

bool save_bookmark_atomic(const std::filesystem::path& path, const BookmarkV1& bookmark);
std::optional<BookmarkV1> load_bookmark(const std::filesystem::path& path);

} // namespace rsvp::storage

#include "rsvp/storage/state_store.hpp"

#include <array>
#include <charconv>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_map>

namespace rsvp::storage {
namespace {

constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

std::string to_string(FixationMode mode) {
    switch (mode) {
    case FixationMode::center:
        return "center";
    case FixationMode::orp:
        return "orp";
    }
    return "center";
}

std::string to_string(PunctuationMode mode) {
    switch (mode) {
    case PunctuationMode::off:
        return "off";
    case PunctuationMode::gentle:
        return "gentle";
    }
    return "gentle";
}

std::optional<FixationMode> parse_fixation_mode(std::string_view value) {
    if (value == "center") {
        return FixationMode::center;
    }
    if (value == "orp") {
        return FixationMode::orp;
    }
    return std::nullopt;
}

std::optional<PunctuationMode> parse_punctuation_mode(std::string_view value) {
    if (value == "off") {
        return PunctuationMode::off;
    }
    if (value == "gentle") {
        return PunctuationMode::gentle;
    }
    return std::nullopt;
}

template <typename Int>
std::optional<Int> parse_decimal(std::string_view value) {
    Int parsed{};
    const auto* begin = value.data();
    const auto* end = value.data() + value.size();
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end) {
        return std::nullopt;
    }
    return parsed;
}

std::unordered_map<std::string, std::string> parse_key_values(std::string_view serialized) {
    std::unordered_map<std::string, std::string> values;
    std::size_t line_start = 0;
    while (line_start <= serialized.size()) {
        const auto line_end = serialized.find('\n', line_start);
        const auto length = line_end == std::string_view::npos
                                ? serialized.size() - line_start
                                : line_end - line_start;
        const auto line = serialized.substr(line_start, length);
        if (!line.empty()) {
            const auto separator = line.find('=');
            if (separator == std::string_view::npos) {
                values.clear();
                return values;
            }
            values.emplace(std::string(line.substr(0, separator)),
                           std::string(line.substr(separator + 1)));
        }

        if (line_end == std::string_view::npos) {
            break;
        }
        line_start = line_end + 1;
    }
    return values;
}

std::optional<std::string> get_value(const std::unordered_map<std::string, std::string>& values,
                                     const char* key) {
    const auto it = values.find(key);
    if (it == values.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::optional<std::string> read_file(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
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

bool write_atomic(const std::filesystem::path& path, std::string_view data) {
    std::error_code error;
    const auto directory = path.parent_path();
    if (!directory.empty()) {
        std::filesystem::create_directories(directory, error);
        if (error) {
            return false;
        }
    }

    const auto tmp = path.string() + ".tmp";
    {
        std::ofstream stream(tmp, std::ios::binary | std::ios::trunc);
        if (!stream) {
            return false;
        }
        stream.write(data.data(), static_cast<std::streamsize>(data.size()));
        stream.flush();
        if (!stream) {
            return false;
        }
    }

    if (std::rename(tmp.c_str(), path.string().c_str()) != 0) {
        std::filesystem::remove(tmp, error);
        return false;
    }
    return true;
}

} // namespace

std::uint64_t content_hash(std::string_view source) {
    std::uint64_t hash = kFnvOffsetBasis;
    for (const auto ch : source) {
        hash ^= static_cast<unsigned char>(ch);
        hash *= kFnvPrime;
    }
    return hash;
}

std::string to_hex(std::uint64_t value) {
    constexpr std::array<char, 16> digits{
        '0', '1', '2', '3', '4', '5', '6', '7',
        '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
    std::string result(16, '0');
    for (std::size_t i = 0; i < result.size(); ++i) {
        const auto shift = (result.size() - i - 1) * 4;
        result[i] = digits[(value >> shift) & 0x0F];
    }
    return result;
}

std::optional<std::uint64_t> parse_hex_u64(std::string_view value) {
    if (value.size() != 16) {
        return std::nullopt;
    }

    std::uint64_t parsed = 0;
    for (const auto ch : value) {
        parsed <<= 4U;
        if (ch >= '0' && ch <= '9') {
            parsed |= static_cast<std::uint64_t>(ch - '0');
        } else if (ch >= 'a' && ch <= 'f') {
            parsed |= static_cast<std::uint64_t>(ch - 'a' + 10);
        } else if (ch >= 'A' && ch <= 'F') {
            parsed |= static_cast<std::uint64_t>(ch - 'A' + 10);
        } else {
            return std::nullopt;
        }
    }
    return parsed;
}

std::string serialize_settings(const SettingsV1& settings) {
    std::ostringstream stream;
    stream << "schema_version=" << SettingsV1::kSchemaVersion << '\n'
           << "wpm=" << settings.wpm << '\n'
           << "font_scale=" << settings.font_scale << '\n'
           << "fixation_mode=" << to_string(settings.fixation_mode) << '\n'
           << "punctuation_mode=" << to_string(settings.punctuation_mode) << '\n';
    return stream.str();
}

std::optional<SettingsV1> parse_settings(std::string_view serialized) {
    const auto values = parse_key_values(serialized);
    if (values.empty()) {
        return std::nullopt;
    }

    const auto schema_value = get_value(values, "schema_version");
    const auto wpm_value = get_value(values, "wpm");
    const auto font_scale_value = get_value(values, "font_scale");
    const auto fixation_value = get_value(values, "fixation_mode");
    const auto punctuation_value = get_value(values, "punctuation_mode");

    const auto schema = schema_value ? parse_decimal<int>(*schema_value) : std::nullopt;
    const auto wpm = wpm_value ? parse_decimal<int>(*wpm_value) : std::nullopt;
    const auto font_scale = font_scale_value ? parse_decimal<int>(*font_scale_value)
                                             : std::nullopt;
    const auto fixation = fixation_value ? parse_fixation_mode(*fixation_value)
                                         : std::nullopt;
    const auto punctuation = punctuation_value ? parse_punctuation_mode(*punctuation_value)
                                               : std::nullopt;

    if (!schema || *schema != SettingsV1::kSchemaVersion || !wpm || !font_scale ||
        !fixation || !punctuation) {
        return std::nullopt;
    }

    SettingsV1 settings;
    settings.wpm = *wpm;
    settings.font_scale = *font_scale;
    settings.fixation_mode = *fixation;
    settings.punctuation_mode = *punctuation;
    return settings;
}

std::string serialize_bookmark(const BookmarkV1& bookmark) {
    std::ostringstream stream;
    stream << "schema_version=" << BookmarkV1::kSchemaVersion << '\n'
           << "doc_id=" << bookmark.doc_id << '\n'
           << "content_hash=" << to_hex(bookmark.content_hash) << '\n'
           << "source_byte_offset=" << bookmark.source_byte_offset << '\n'
           << "token_index=" << bookmark.token_index << '\n'
           << "frame_index=" << bookmark.frame_index << '\n'
           << "source_token_index=" << bookmark.source_token_index << '\n'
           << "chunk_index=" << bookmark.chunk_index << '\n'
           << "updated_at=" << bookmark.updated_at << '\n';
    return stream.str();
}

std::optional<BookmarkV1> parse_bookmark(std::string_view serialized) {
    const auto values = parse_key_values(serialized);
    if (values.empty()) {
        return std::nullopt;
    }

    const auto schema_value = get_value(values, "schema_version");
    const auto doc_id = get_value(values, "doc_id");
    const auto hash_value = get_value(values, "content_hash");
    const auto offset_value = get_value(values, "source_byte_offset");
    const auto token_value = get_value(values, "token_index");
    const auto frame_value = get_value(values, "frame_index");
    const auto source_token_value = get_value(values, "source_token_index");
    const auto chunk_value = get_value(values, "chunk_index");
    const auto updated_value = get_value(values, "updated_at");

    const auto schema = schema_value ? parse_decimal<int>(*schema_value) : std::nullopt;
    const auto hash = hash_value ? parse_hex_u64(*hash_value) : std::nullopt;
    const auto offset = offset_value ? parse_decimal<std::size_t>(*offset_value)
                                     : std::nullopt;
    const auto token = token_value ? parse_decimal<std::size_t>(*token_value)
                                   : std::nullopt;
    const auto frame = frame_value ? parse_decimal<std::size_t>(*frame_value)
                                   : token;
    const auto source_token =
        source_token_value ? parse_decimal<std::size_t>(*source_token_value)
                           : token;
    const auto chunk = chunk_value ? parse_decimal<std::size_t>(*chunk_value)
                                   : std::optional<std::size_t>{0};
    const auto updated = updated_value ? parse_decimal<std::uint64_t>(*updated_value)
                                       : std::nullopt;

    if (!schema || *schema != BookmarkV1::kSchemaVersion || !doc_id ||
        doc_id->empty() || !hash || !offset || !token || !frame ||
        !source_token || !chunk || !updated) {
        return std::nullopt;
    }

    BookmarkV1 bookmark;
    bookmark.doc_id = *doc_id;
    bookmark.content_hash = *hash;
    bookmark.source_byte_offset = *offset;
    bookmark.token_index = *token;
    bookmark.frame_index = *frame;
    bookmark.source_token_index = *source_token;
    bookmark.chunk_index = *chunk;
    bookmark.updated_at = *updated;
    return bookmark;
}

bool save_settings_atomic(const std::filesystem::path& path, const SettingsV1& settings) {
    return write_atomic(path, serialize_settings(settings));
}

std::optional<SettingsV1> load_settings(const std::filesystem::path& path) {
    const auto content = read_file(path);
    if (!content) {
        return std::nullopt;
    }
    return parse_settings(*content);
}

bool save_bookmark_atomic(const std::filesystem::path& path, const BookmarkV1& bookmark) {
    return write_atomic(path, serialize_bookmark(bookmark));
}

std::optional<BookmarkV1> load_bookmark(const std::filesystem::path& path) {
    const auto content = read_file(path);
    if (!content) {
        return std::nullopt;
    }
    return parse_bookmark(*content);
}

} // namespace rsvp::storage

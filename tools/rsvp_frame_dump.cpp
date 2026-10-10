#include "rsvp/text/display_frame.hpp"
#include "rsvp/text/tokenizer.hpp"

#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

namespace {

using rsvp::text::DisplayFrameOptions;
using rsvp::text::PunctuationFlags;
using rsvp::text::has_flag;
using rsvp::text::make_display_frames;
using rsvp::text::tokenize;

std::string read_stdin() {
    std::ostringstream buffer;
    buffer << std::cin.rdbuf();
    return buffer.str();
}

void write_json_string(std::ostream& out, std::string_view value) {
    out << '"';
    for (const unsigned char byte : value) {
        switch (byte) {
        case '"':
            out << "\\\"";
            break;
        case '\\':
            out << "\\\\";
            break;
        case '\b':
            out << "\\b";
            break;
        case '\f':
            out << "\\f";
            break;
        case '\n':
            out << "\\n";
            break;
        case '\r':
            out << "\\r";
            break;
        case '\t':
            out << "\\t";
            break;
        default:
            if (byte < 0x20) {
                static constexpr char kHex[] = "0123456789abcdef";
                out << "\\u00" << kHex[(byte >> 4U) & 0x0FU] << kHex[byte & 0x0FU];
            } else {
                out << static_cast<char>(byte);
            }
            break;
        }
    }
    out << '"';
}

void write_flags(std::ostream& out, PunctuationFlags flags) {
    bool first = true;
    const auto write_flag = [&](std::string_view name) {
        if (!first) {
            out << ", ";
        }
        write_json_string(out, name);
        first = false;
    };

    out << '[';
    if (has_flag(flags, PunctuationFlags::comma)) {
        write_flag("comma");
    }
    if (has_flag(flags, PunctuationFlags::sentence_end)) {
        write_flag("sentence_end");
    }
    if (has_flag(flags, PunctuationFlags::paragraph_break_before)) {
        write_flag("paragraph_break_before");
    }
    if (has_flag(flags, PunctuationFlags::invalid_utf8)) {
        write_flag("invalid_utf8");
    }
    out << ']';
}

} // namespace

int main(int argc, char* argv[]) {
    DisplayFrameOptions options;
    std::string source;

    for (int index = 1; index < argc; ++index) {
        const std::string_view arg = argv[index];
        if (arg == "--max-codepoints" && index + 1 < argc) {
            const auto value = std::string_view(argv[++index]);
            std::size_t parsed = 0;
            for (const char ch : value) {
                if (ch < '0' || ch > '9') {
                    std::cerr << "Invalid --max-codepoints value\n";
                    return 2;
                }
                parsed = parsed * 10 + static_cast<std::size_t>(ch - '0');
            }
            options.max_codepoints_per_frame = parsed;
        } else if (arg == "--no-chunk") {
            options.chunk_long_tokens = false;
        } else if (arg == "--text" && index + 1 < argc) {
            source = argv[++index];
        } else if (arg == "--help") {
            std::cout << "Usage: rsvp_frame_dump [--max-codepoints N] [--no-chunk] "
                         "[--text TEXT]\n"
                         "If --text is omitted, UTF-8 text is read from stdin.\n";
            return 0;
        } else {
            std::cerr << "Unknown or incomplete argument: " << arg << '\n';
            return 2;
        }
    }

    if (source.empty()) {
        source = read_stdin();
    }

    const auto frames = make_display_frames(tokenize(source), options);

    std::cout << "{\n  \"schema\": \"rsvp-display-frames-v1\",\n";
    std::cout << "  \"max_codepoints_per_frame\": " << options.max_codepoints_per_frame
              << ",\n";
    std::cout << "  \"frames\": [\n";
    for (std::size_t index = 0; index < frames.size(); ++index) {
        const auto& frame = frames[index];
        std::cout << "    {\n";
        std::cout << "      \"index\": " << index << ",\n";
        std::cout << "      \"text\": ";
        write_json_string(std::cout, frame.text_utf8);
        std::cout << ",\n";
        std::cout << "      \"source_token_index\": " << frame.source_token_index << ",\n";
        std::cout << "      \"chunk_index\": " << frame.chunk_index << ",\n";
        std::cout << "      \"chunk_count\": " << frame.chunk_count << ",\n";
        std::cout << "      \"source_byte_start\": " << frame.source_byte_start << ",\n";
        std::cout << "      \"source_byte_length\": " << frame.source_byte_length << ",\n";
        std::cout << "      \"orp_codepoint_index\": " << frame.orp_codepoint_index << ",\n";
        std::cout << "      \"centered_fallback\": "
                  << (frame.centered_fallback ? "true" : "false") << ",\n";
        std::cout << "      \"continuation_before\": "
                  << (frame.continuation_before ? "true" : "false") << ",\n";
        std::cout << "      \"continuation_after\": "
                  << (frame.continuation_after ? "true" : "false") << ",\n";
        std::cout << "      \"punctuation_flags\": ";
        write_flags(std::cout, frame.punctuation_flags);
        std::cout << '\n';
        std::cout << "    }" << (index + 1 == frames.size() ? "" : ",") << '\n';
    }
    std::cout << "  ]\n}\n";
    return 0;
}

#include "rsvp/text/tokenizer.hpp"

#include <array>

namespace rsvp::text {
namespace {

constexpr std::string_view kReplacement = "\xEF\xBF\xBD";

struct DecodedCodePoint {
    char32_t value = 0;
    std::size_t length = 0;
    bool valid = false;
};

bool is_continuation(unsigned char byte) {
    return (byte & 0xC0U) == 0x80U;
}

DecodedCodePoint decode_one(std::string_view source, std::size_t offset) {
    const auto first = static_cast<unsigned char>(source[offset]);
    const auto remaining = source.size() - offset;

    if (first < 0x80U) {
        return {first, 1, true};
    }

    std::size_t length = 0;
    char32_t value = 0;
    char32_t minimum = 0;

    if ((first & 0xE0U) == 0xC0U) {
        length = 2;
        value = first & 0x1FU;
        minimum = 0x80;
    } else if ((first & 0xF0U) == 0xE0U) {
        length = 3;
        value = first & 0x0FU;
        minimum = 0x800;
    } else if ((first & 0xF8U) == 0xF0U) {
        length = 4;
        value = first & 0x07U;
        minimum = 0x10000;
    } else {
        return {0, 1, false};
    }

    if (remaining < length) {
        return {0, 1, false};
    }

    for (std::size_t i = 1; i < length; ++i) {
        const auto byte = static_cast<unsigned char>(source[offset + i]);
        if (!is_continuation(byte)) {
            return {0, 1, false};
        }
        value = (value << 6U) | (byte & 0x3FU);
    }

    const bool surrogate = value >= 0xD800 && value <= 0xDFFF;
    const bool outside_unicode = value > 0x10FFFF;
    if (value < minimum || surrogate || outside_unicode) {
        return {0, 1, false};
    }

    return {value, length, true};
}

bool is_whitespace(char32_t value) {
    switch (value) {
    case U'\t':
    case U'\n':
    case U'\v':
    case U'\f':
    case U'\r':
    case U' ':
    case 0x0085:
    case 0x00A0:
    case 0x1680:
    case 0x2000:
    case 0x2001:
    case 0x2002:
    case 0x2003:
    case 0x2004:
    case 0x2005:
    case 0x2006:
    case 0x2007:
    case 0x2008:
    case 0x2009:
    case 0x200A:
    case 0x2028:
    case 0x2029:
    case 0x202F:
    case 0x205F:
    case 0x3000:
        return true;
    default:
        return false;
    }
}

bool is_comma_like(char32_t value) {
    return value == U',' || value == U';' || value == U':' || value == 0x060C ||
           value == 0x3001 || value == 0xFF0C || value == 0xFF1B || value == 0xFF1A;
}

bool is_sentence_end(char32_t value) {
    return value == U'.' || value == U'!' || value == U'?' || value == 0x3002 ||
           value == 0xFF01 || value == 0xFF1F || value == 0x061F;
}

bool is_line_break(char32_t value) {
    return value == U'\n' || value == U'\r' || value == 0x2028 || value == 0x2029;
}

} // namespace

PunctuationFlags operator|(PunctuationFlags lhs, PunctuationFlags rhs) {
    return static_cast<PunctuationFlags>(
        static_cast<std::uint8_t>(lhs) | static_cast<std::uint8_t>(rhs));
}

PunctuationFlags& operator|=(PunctuationFlags& lhs, PunctuationFlags rhs) {
    lhs = lhs | rhs;
    return lhs;
}

bool has_flag(PunctuationFlags value, PunctuationFlags flag) {
    return (static_cast<std::uint8_t>(value) & static_cast<std::uint8_t>(flag)) != 0;
}

std::vector<Token> tokenize(std::string_view source) {
    std::vector<Token> tokens;
    Token current;
    bool in_token = false;
    int pending_line_breaks = 0;

    const auto finish_token = [&]() {
        if (in_token) {
            tokens.push_back(current);
            current = Token{};
            in_token = false;
        }
    };

    for (std::size_t offset = 0; offset < source.size();) {
        const auto decoded = decode_one(source, offset);

        if (!decoded.valid) {
            finish_token();
            Token invalid;
            invalid.text_utf8 = std::string(kReplacement);
            invalid.source_byte_start = offset;
            invalid.source_byte_length = 1;
            invalid.punctuation_flags = PunctuationFlags::invalid_utf8;
            if (pending_line_breaks >= 2) {
                invalid.punctuation_flags |= PunctuationFlags::paragraph_break_before;
            }
            tokens.push_back(invalid);
            pending_line_breaks = 0;
            ++offset;
            continue;
        }

        if (is_whitespace(decoded.value)) {
            finish_token();
            if (is_line_break(decoded.value)) {
                if (decoded.value == U'\r' && offset + decoded.length < source.size() &&
                    source[offset + decoded.length] == '\n') {
                    offset += decoded.length;
                    const auto next = decode_one(source, offset);
                    if (next.valid && next.value == U'\n') {
                        offset += next.length;
                    }
                } else {
                    offset += decoded.length;
                }
                ++pending_line_breaks;
            } else {
                offset += decoded.length;
            }
            continue;
        }

        if (!in_token) {
            current = Token{};
            current.source_byte_start = offset;
            if (pending_line_breaks >= 2) {
                current.punctuation_flags |= PunctuationFlags::paragraph_break_before;
            }
            pending_line_breaks = 0;
            in_token = true;
        }

        current.text_utf8.append(source.substr(offset, decoded.length));
        current.source_byte_length =
            offset + decoded.length - current.source_byte_start;
        if (is_comma_like(decoded.value)) {
            current.punctuation_flags |= PunctuationFlags::comma;
        }
        if (is_sentence_end(decoded.value)) {
            current.punctuation_flags |= PunctuationFlags::sentence_end;
        }
        offset += decoded.length;
    }

    finish_token();
    return tokens;
}

} // namespace rsvp::text

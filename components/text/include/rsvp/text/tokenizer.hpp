#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rsvp::text {

enum class PunctuationFlags : std::uint8_t {
    none = 0,
    comma = 1 << 0,
    sentence_end = 1 << 1,
    paragraph_break_before = 1 << 2,
    invalid_utf8 = 1 << 3,
};

struct Token {
    std::string text_utf8;
    std::size_t source_byte_start = 0;
    std::size_t source_byte_length = 0;
    PunctuationFlags punctuation_flags = PunctuationFlags::none;
};

PunctuationFlags operator|(PunctuationFlags lhs, PunctuationFlags rhs);
PunctuationFlags& operator|=(PunctuationFlags& lhs, PunctuationFlags rhs);
bool has_flag(PunctuationFlags value, PunctuationFlags flag);

std::vector<Token> tokenize(std::string_view source);

} // namespace rsvp::text

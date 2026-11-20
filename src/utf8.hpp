#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace layoutfix::utf8 {

std::size_t char_count(std::string_view text);
std::string casefold(std::string_view text);
bool is_word_char(char32_t cp);
bool is_letter(char32_t cp);

// One Unicode scalar from text at offset. Advances offset. Returns 0 on bad input.
char32_t decode_at(std::string_view text, std::size_t& offset);

}  // namespace layoutfix::utf8

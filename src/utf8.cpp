#include "utf8.hpp"

namespace layoutfix::utf8 {
namespace {

void append_utf8(std::string& out, char32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

char32_t fold_cp(char32_t cp) {
    if (cp >= U'A' && cp <= U'Z') {
        return cp - U'A' + U'a';
    }
    if (cp >= 0x0410 && cp <= 0x042F) {
        return cp + 0x20;
    }
    switch (cp) {
        case 0x0401:
            return 0x0451;  // Ё
        case 0x0404:
            return 0x0454;  // Є
        case 0x0406:
            return 0x0456;  // І
        case 0x0407:
            return 0x0457;  // Ї
        case 0x0490:
            return 0x0491;  // Ґ
        default:
            return cp;
    }
}

}  // namespace

char32_t decode_at(std::string_view text, std::size_t& offset) {
    if (offset >= text.size()) {
        return 0;
    }
    const auto b0 = static_cast<unsigned char>(text[offset]);
    if (b0 < 0x80) {
        ++offset;
        return b0;
    }
    if ((b0 & 0xE0) == 0xC0 && offset + 1 < text.size()) {
        const auto b1 = static_cast<unsigned char>(text[offset + 1]);
        offset += 2;
        return (static_cast<char32_t>(b0 & 0x1F) << 6) | (b1 & 0x3F);
    }
    if ((b0 & 0xF0) == 0xE0 && offset + 2 < text.size()) {
        const auto b1 = static_cast<unsigned char>(text[offset + 1]);
        const auto b2 = static_cast<unsigned char>(text[offset + 2]);
        offset += 3;
        return (static_cast<char32_t>(b0 & 0x0F) << 12) |
               (static_cast<char32_t>(b1 & 0x3F) << 6) | (b2 & 0x3F);
    }
    if ((b0 & 0xF8) == 0xF0 && offset + 3 < text.size()) {
        const auto b1 = static_cast<unsigned char>(text[offset + 1]);
        const auto b2 = static_cast<unsigned char>(text[offset + 2]);
        const auto b3 = static_cast<unsigned char>(text[offset + 3]);
        offset += 4;
        return (static_cast<char32_t>(b0 & 0x07) << 18) |
               (static_cast<char32_t>(b1 & 0x3F) << 12) |
               (static_cast<char32_t>(b2 & 0x3F) << 6) | (b3 & 0x3F);
    }
    ++offset;
    return 0;
}

std::size_t char_count(std::string_view text) {
    std::size_t count = 0;
    std::size_t i = 0;
    while (i < text.size()) {
        decode_at(text, i);
        ++count;
    }
    return count;
}

std::string casefold(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    std::size_t i = 0;
    while (i < text.size()) {
        append_utf8(out, fold_cp(decode_at(text, i)));
    }
    return out;
}

bool is_letter(char32_t cp) {
    if (cp >= U'A' && cp <= U'Z') {
        return true;
    }
    if (cp >= U'a' && cp <= U'z') {
        return true;
    }
    if (cp >= 0x0400 && cp <= 0x04FF) {
        return true;
    }
    return false;
}

bool is_word_char(char32_t cp) {
    if (cp >= U'0' && cp <= U'9') {
        return true;
    }
    if (cp == U'\'' || cp == U'-' || cp == 0x2019 || cp == 0x02BC) {
        return true;
    }
    return is_letter(cp);
}

}  // namespace layoutfix::utf8

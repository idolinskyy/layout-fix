#pragma once

#include "lang.hpp"
#include "utf8.hpp"

#include <string_view>

namespace layoutfix {

struct GroupLetters {
    int latin_letters = 0;
    int uk_specific = 0;
    int ru_specific = 0;
    int other_cyrillic = 0;
};

inline bool is_uk_specific_letter(char32_t cp) {
    return cp == 0x0404 || cp == 0x0454 ||  // Є є
           cp == 0x0406 || cp == 0x0456 ||  // І і
           cp == 0x0407 || cp == 0x0457 ||  // Ї ї
           cp == 0x0490 || cp == 0x0491;    // Ґ ґ
}

inline bool is_ru_specific_letter(char32_t cp) {
    return cp == 0x0401 || cp == 0x0451 ||  // Ё ё
           cp == 0x042A || cp == 0x044A ||  // Ъ ъ
           cp == 0x042B || cp == 0x044B ||  // Ы ы
           cp == 0x042D || cp == 0x044D;    // Э э
}

inline void note_group_letter(GroupLetters& letters, std::string_view text) {
    std::size_t offset = 0;
    while (offset < text.size()) {
        const char32_t cp = utf8::decode_at(text, offset);
        if (is_uk_specific_letter(cp)) {
            ++letters.uk_specific;
        } else if (is_ru_specific_letter(cp)) {
            ++letters.ru_specific;
        } else if (cp >= 0x0400 && cp <= 0x04FF) {
            ++letters.other_cyrillic;
        } else if ((cp >= U'A' && cp <= U'Z') || (cp >= U'a' && cp <= U'z')) {
            ++letters.latin_letters;
        }
    }
}

// Мову групи визначають її літери, а не позиція в списку XKB.
// Англійська не мусить бути групою 0: українська першою теж розпізнається.
inline Lang classify_group_letters(const GroupLetters& letters) {
    if (letters.uk_specific > 0 &&
        letters.uk_specific >= letters.ru_specific) {
        return Lang::Uk;
    }
    if (letters.ru_specific > 0) {
        return Lang::Ru;
    }
    if (letters.latin_letters >= 10 && letters.other_cyrillic == 0 &&
        letters.uk_specific == 0 && letters.ru_specific == 0) {
        return Lang::En;
    }
    return Lang::Other;
}

}  // namespace layoutfix

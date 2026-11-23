#pragma once

#include "input/word_buffer.hpp"
#include "lang.hpp"

#include <array>
#include <string>
#include <utility>
#include <vector>

namespace layoutfix {

struct PendingWord {
    std::array<std::string, 3> forms{};
    std::vector<Stroke> strokes;
    Lang lang = Lang::Other;
};

inline void remember_pending(std::vector<PendingWord>& pending, PendingWord word) {
    pending.push_back(std::move(word));
    if (pending.size() > 3) {
        pending.erase(pending.begin());
    }
}

struct PendingRestore {
    bool restored = false;
    PendingWord word;
};

// Backspace після пробілу повертає лише останнє коротке слово в буфер,
// без кінцевого пробілу. Попередні слова лишаються в черзі.
// Якщо стерто літеру живого слова, чергу не чіпаємо.
inline PendingRestore restore_pending_on_backspace(std::vector<PendingWord>& pending,
                                                   bool ate_live_word,
                                                   bool buffer_empty) {
    PendingRestore result;
    if (ate_live_word || !buffer_empty || pending.empty()) {
        return result;
    }
    PendingWord word = std::move(pending.back());
    pending.pop_back();
    while (!word.strokes.empty() && !word.strokes.back().word) {
        word.strokes.pop_back();
    }
    if (word.strokes.empty()) {
        return result;
    }
    result.restored = true;
    result.word = std::move(word);
    return result;
}

}  // namespace layoutfix

#pragma once

#include "input/word_buffer.hpp"
#include "lang.hpp"

#include <string>
#include <utility>
#include <vector>

namespace layoutfix {

struct Undo {
    bool valid = false;
    int extra = 0;
    bool paste = false;
    int base_delete = 0;
    std::string paste_text;
    std::vector<Stroke> original;
    std::vector<Stroke> shown;
    int source_index = -1;
    Lang source_lang = Lang::Other;
    Lang shown_lang = Lang::Other;

    void clear() { *this = Undo{}; }

    void arm_keys(std::vector<Stroke> original_strokes,
                  std::vector<Stroke> shown_strokes, int source_index_in,
                  Lang source, Lang shown_language) {
        const int base = static_cast<int>(shown_strokes.size());
        clear();
        valid = true;
        base_delete = base;
        original = std::move(original_strokes);
        shown = std::move(shown_strokes);
        source_index = source_index_in;
        source_lang = source;
        shown_lang = shown_language;
    }

    void arm_paste(std::string original_text, int shown_chars,
                   int source_index_in, Lang source, Lang shown_language) {
        clear();
        valid = true;
        paste = true;
        paste_text = std::move(original_text);
        base_delete = shown_chars;
        source_index = source_index_in;
        source_lang = source;
        shown_lang = shown_language;
    }

    void on_typed_key() {
        if (valid) {
            ++extra;
        }
    }

    // true: Backspace уже стер символ самого виправлення, а не набраний після нього хвіст.
    bool on_backspace() {
        if (!valid) {
            return false;
        }
        if (extra > 0) {
            --extra;
            return false;
        }
        valid = false;
        return true;
    }

    void on_word_committed() { clear(); }

    int delete_count() const { return base_delete + extra; }
};

}  // namespace layoutfix

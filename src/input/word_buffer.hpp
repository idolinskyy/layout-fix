#pragma once

#include "lang.hpp"

#include <array>
#include <string>
#include <vector>

namespace layoutfix {

struct Stroke {
    unsigned keycode = 0;
    bool shift = false;
    bool word = false;
    std::string current;
    std::string other;
    std::array<std::string, 3> forms;
};

class WordBuffer {
public:
    void clear();
    void push_word(unsigned keycode, bool shift, Lang current_language,
                   std::array<std::string, 3> forms);
    void push_boundary(unsigned keycode, bool shift, std::string text);
    void assign(int group, Lang language, std::array<std::string, 3> forms,
                std::vector<Stroke> strokes);
    bool pop();

    bool empty() const { return strokes_.empty(); }
    int group() const { return group_; }
    void set_group(int group) { group_ = group; }
    const std::string& text(Lang language) const { return texts_[lang_index(language)]; }
    const std::vector<Stroke>& strokes() const { return strokes_; }
    bool has_word() const {
        return current_language_ != Lang::Other &&
               !texts_[lang_index(current_language_)].empty();
    }

private:
    std::vector<Stroke> strokes_;
    std::array<std::string, 3> texts_;
    Lang current_language_ = Lang::Other;
    int group_ = -1;
};

// Збирає готовий UTF-8 рядок для цільової мови з послідовності Stroke
// (для атомарної вставки багатослівного виправлення через буфер обміну).
std::string build_replacement_text(const std::vector<Stroke>& strokes,
                                   Lang target);

}  // namespace layoutfix

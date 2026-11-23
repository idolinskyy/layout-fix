#include "input/word_buffer.hpp"

namespace layoutfix {

void WordBuffer::clear() {
    strokes_.clear();
    for (auto& text : texts_) {
        text.clear();
    }
    current_language_ = Lang::Other;
    group_ = -1;
}

void WordBuffer::push_word(unsigned keycode, bool shift, Lang current_language,
                           std::array<std::string, 3> forms) {
    current_language_ = current_language;
    for (std::size_t i = 0; i < forms.size(); ++i) {
        texts_[i] += forms[i];
    }
    strokes_.push_back(Stroke{keycode, shift, true,
                              forms[lang_index(current_language)], {},
                              std::move(forms)});
}

void WordBuffer::push_boundary(unsigned keycode, bool shift, std::string text) {
    strokes_.push_back(Stroke{keycode, shift, false, std::move(text), {}, {}});
}

void WordBuffer::assign(int group, Lang language, std::array<std::string, 3> forms,
                        std::vector<Stroke> strokes) {
    strokes_ = std::move(strokes);
    texts_ = std::move(forms);
    current_language_ = language;
    group_ = group;
}

bool WordBuffer::pop() {
    if (strokes_.empty() || !strokes_.back().word) {
        return false;
    }
    const auto& last = strokes_.back();
    for (std::size_t i = 0; i < last.forms.size(); ++i) {
        if (texts_[i].size() >= last.forms[i].size()) {
            texts_[i].resize(texts_[i].size() - last.forms[i].size());
        }
    }
    strokes_.pop_back();
    if (strokes_.empty()) {
        current_language_ = Lang::Other;
        group_ = -1;
    }
    return true;
}

std::string build_replacement_text(const std::vector<Stroke>& strokes,
                                   Lang target) {
    std::string out;
    if (target == Lang::Other) {
        return out;
    }
    const std::size_t idx = lang_index(target);
    for (const auto& stroke : strokes) {
        if (stroke.word) {
            out += stroke.forms[idx];
        } else if (stroke.current == "\r") {
            out += '\n';
        } else {
            out += stroke.current;
        }
    }
    return out;
}

}  // namespace layoutfix

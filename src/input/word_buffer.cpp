#include "input/word_buffer.hpp"

namespace layoutfix {

void WordBuffer::clear() {
    strokes_.clear();
    current_.clear();
    other_.clear();
    group_ = -1;
}

void WordBuffer::push_word(unsigned keycode, bool shift, std::string current,
                           std::string other) {
    current_ += current;
    other_ += other;
    strokes_.push_back(Stroke{keycode, shift, true, std::move(current), std::move(other)});
}

void WordBuffer::push_boundary(unsigned keycode, bool shift, std::string text) {
    strokes_.push_back(Stroke{keycode, shift, false, std::move(text), {}});
}

void WordBuffer::pop() {
    if (strokes_.empty() || !strokes_.back().word) {
        return;
    }
    const auto& last = strokes_.back();
    if (current_.size() >= last.current.size()) {
        current_.resize(current_.size() - last.current.size());
    }
    if (other_.size() >= last.other.size()) {
        other_.resize(other_.size() - last.other.size());
    }
    strokes_.pop_back();
    if (strokes_.empty()) {
        group_ = -1;
    }
}

}  // namespace layoutfix

#pragma once

#include "input/word_buffer.hpp"
#include "x11/connection.hpp"

#include <vector>

namespace layoutfix {

class Retype {
public:
    explicit Retype(XConnection& x11) : x11_(x11) {}

    bool init();
    // Скільки подій XRecord очікувати у відповідь.
    int replace(int delete_count, const std::vector<Stroke>& strokes);
    unsigned backspace_keycode() const { return backspace_; }

private:
    int tap(unsigned keycode);
    int set_shift(bool down);

    XConnection& x11_;
    unsigned backspace_ = 0;
    unsigned shift_ = 0;
    bool shift_down_ = false;
};

}  // namespace layoutfix

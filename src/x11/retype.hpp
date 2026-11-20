#pragma once

#include "input/echoes.hpp"
#include "input/word_buffer.hpp"
#include "x11/connection.hpp"

#include <vector>

namespace layoutfix {

class Retype {
public:
    explicit Retype(XConnection& x11) : x11_(x11) {}

    bool init();
    // Дописує в echoes press/release у тому порядку, в якому їх побачить XRecord.
    void replace(int delete_count, const std::vector<Stroke>& strokes, std::vector<EchoKey>& echoes);
    unsigned backspace_keycode() const { return backspace_; }

private:
    void tap(unsigned keycode, std::vector<EchoKey>& echoes);
    void set_shift(bool down, std::vector<EchoKey>& echoes);

    XConnection& x11_;
    unsigned backspace_ = 0;
    unsigned shift_ = 0;
    bool shift_down_ = false;
};

}  // namespace layoutfix

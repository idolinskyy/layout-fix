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
    void send_ctrl_c(std::vector<EchoKey>& echoes);
    void send_ctrl_v(std::vector<EchoKey>& echoes);
    unsigned backspace_keycode() const { return backspace_; }

private:
    void tap(unsigned keycode, std::vector<EchoKey>& echoes);
    void set_shift(bool down, std::vector<EchoKey>& echoes);
    void release_if_down(unsigned keycode, char map[32], std::vector<EchoKey>& echoes);
    void send_ctrl_shortcut(unsigned keycode, std::vector<EchoKey>& echoes);

    XConnection& x11_;
    unsigned backspace_ = 0;
    unsigned shift_ = 0;
    unsigned shift_r_ = 0;
    unsigned ctrl_ = 0;
    unsigned c_key_ = 0;
    unsigned v_key_ = 0;
    std::vector<unsigned> modifiers_;
    bool shift_down_ = false;
};

}  // namespace layoutfix

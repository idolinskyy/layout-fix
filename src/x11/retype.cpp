#include "x11/retype.hpp"

#include "log.hpp"

#include <X11/XKBlib.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>

namespace layoutfix {
namespace {

bool key_down(const char map[32], unsigned keycode) {
    return (map[keycode / 8] & (1u << (keycode % 8))) != 0;
}

}  // namespace

bool Retype::init() {
    auto* dpy = x11_.ctrl();
    backspace_ = XKeysymToKeycode(dpy, XK_BackSpace);
    shift_ = XKeysymToKeycode(dpy, XK_Shift_L);
    if (backspace_ == 0 || shift_ == 0) {
        log::error("не знайдено клавіші BackSpace або Shift");
        return false;
    }
    return true;
}

void Retype::tap(unsigned keycode, std::vector<EchoKey>& echoes) {
    auto* dpy = x11_.ctrl();
    XTestFakeKeyEvent(dpy, keycode, True, CurrentTime);
    XTestFakeKeyEvent(dpy, keycode, False, CurrentTime);
    echoes.push_back(EchoKey{keycode, true});
    echoes.push_back(EchoKey{keycode, false});
}

void Retype::set_shift(bool down, std::vector<EchoKey>& echoes) {
    if (down == shift_down_) {
        return;
    }
    XTestFakeKeyEvent(x11_.ctrl(), shift_, down ? True : False, CurrentTime);
    echoes.push_back(EchoKey{shift_, down});
    shift_down_ = down;
}

void Retype::replace(int delete_count, const std::vector<Stroke>& strokes,
                     std::vector<EchoKey>& echoes) {
    auto* dpy = x11_.ctrl();
    char map[32] = {};
    XQueryKeymap(dpy, map);
    const bool user_shift = key_down(map, shift_);
    shift_down_ = user_shift;

    set_shift(false, echoes);
    for (int i = 0; i < delete_count; ++i) {
        tap(backspace_, echoes);
    }
    for (const auto& stroke : strokes) {
        set_shift(stroke.shift, echoes);
        tap(stroke.keycode, echoes);
    }
    set_shift(user_shift, echoes);
    XFlush(dpy);
}

}  // namespace layoutfix

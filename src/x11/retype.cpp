#include "x11/retype.hpp"

#include "log.hpp"

#include <X11/XKBlib.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>

namespace layoutfix {
namespace {

bool key_down(const char map[32], unsigned keycode) {
    if (keycode >= 256) {
        return false;
    }
    return (map[keycode / 8] & (1u << (keycode % 8))) != 0;
}

void clear_key_down(char map[32], unsigned keycode) {
    if (keycode < 256) {
        map[keycode / 8] &= static_cast<char>(~(1u << (keycode % 8)));
    }
}

}  // namespace

bool Retype::init() {
    auto* dpy = x11_.ctrl();
    backspace_ = XKeysymToKeycode(dpy, XK_BackSpace);
    shift_ = XKeysymToKeycode(dpy, XK_Shift_L);
    shift_r_ = XKeysymToKeycode(dpy, XK_Shift_R);
    ctrl_ = XKeysymToKeycode(dpy, XK_Control_L);
    c_key_ = XKeysymToKeycode(dpy, XK_c);
    v_key_ = XKeysymToKeycode(dpy, XK_v);
    if (backspace_ == 0 || shift_ == 0) {
        log::error("не знайдено клавіші BackSpace або Shift");
        return false;
    }
    modifiers_.clear();
    for (KeySym sym : {XK_Shift_L, XK_Shift_R, XK_Control_L, XK_Control_R,
                       XK_Alt_L, XK_Alt_R, XK_Super_L, XK_Super_R,
                       XK_Meta_L, XK_Meta_R, XK_ISO_Level3_Shift}) {
        if (const unsigned kc = XKeysymToKeycode(dpy, sym); kc != 0) {
            modifiers_.push_back(kc);
        }
    }
    return true;
}

void Retype::tap(unsigned keycode, std::vector<EchoKey>& echoes) {
    auto* dpy = x11_.ctrl();
    XTestFakeKeyEvent(dpy, keycode, True, CurrentTime);
    XTestFakeKeyEvent(dpy, keycode, False, CurrentTime);
    echoes.push_back(EchoKey{keycode, true, false});
    echoes.push_back(EchoKey{keycode, false, false});
}

void Retype::set_shift(bool down, std::vector<EchoKey>& echoes) {
    if (down == shift_down_) {
        return;
    }
    XTestFakeKeyEvent(x11_.ctrl(), shift_, down ? True : False, CurrentTime);
    echoes.push_back(EchoKey{shift_, down, false});
    shift_down_ = down;
}

void Retype::release_if_down(unsigned keycode, char map[32],
                             std::vector<EchoKey>& echoes) {
    if (keycode == 0 || !key_down(map, keycode)) {
        return;
    }
    XTestFakeKeyEvent(x11_.ctrl(), keycode, False, CurrentTime);
    echoes.push_back(EchoKey{keycode, false, true});
    clear_key_down(map, keycode);
}

void Retype::replace(int delete_count, const std::vector<Stroke>& strokes,
                     std::vector<EchoKey>& echoes) {
    if (delete_count <= 0 && strokes.empty()) {
        return;
    }
    auto* dpy = x11_.ctrl();
    char map[32] = {};
    XQueryKeymap(dpy, map);

    // Відпускаємо всі затиснуті модифікатори (Shift/Ctrl/Alt/Super) з optional-echo,
    // щоб вони не спотворювали BackSpace/літери (напр. при Alt+Pause або Shift+Pause)
    // і не вішали чергу відлуння, якщо користувач відпустив клавішу сам.
    for (unsigned mod_kc : modifiers_) {
        release_if_down(mod_kc, map, echoes);
    }
    shift_down_ = false;

    if (delete_count > 0) {
        release_if_down(backspace_, map, echoes);
        for (int i = 0; i < delete_count; ++i) {
            tap(backspace_, echoes);
        }
    }
    for (const auto& stroke : strokes) {
        release_if_down(stroke.keycode, map, echoes);
        set_shift(stroke.shift, echoes);
        tap(stroke.keycode, echoes);
    }
    set_shift(false, echoes);
    XFlush(dpy);
}

void Retype::send_ctrl_shortcut(unsigned keycode, std::vector<EchoKey>& echoes) {
    if (ctrl_ == 0 || keycode == 0) {
        return;
    }
    auto* dpy = x11_.ctrl();
    char map[32] = {};
    XQueryKeymap(dpy, map);
    for (unsigned mod_kc : modifiers_) {
        release_if_down(mod_kc, map, echoes);
    }
    release_if_down(keycode, map, echoes);
    shift_down_ = false;

    XTestFakeKeyEvent(dpy, ctrl_, True, CurrentTime);
    echoes.push_back(EchoKey{ctrl_, true, false});
    tap(keycode, echoes);
    XTestFakeKeyEvent(dpy, ctrl_, False, CurrentTime);
    echoes.push_back(EchoKey{ctrl_, false, false});
    XFlush(dpy);
}

void Retype::send_ctrl_c(std::vector<EchoKey>& echoes) {
    send_ctrl_shortcut(c_key_, echoes);
}

void Retype::send_ctrl_v(std::vector<EchoKey>& echoes) {
    send_ctrl_shortcut(v_key_, echoes);
}

}  // namespace layoutfix

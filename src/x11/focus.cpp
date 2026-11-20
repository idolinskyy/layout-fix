#include "x11/focus.hpp"

#include "log.hpp"

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <cctype>
#include <string_view>

namespace layoutfix {
namespace {

bool contains_ci(std::string_view haystack, std::string_view needle) {
    if (needle.empty() || haystack.size() < needle.size()) {
        return false;
    }
    auto lower = [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    };
    for (std::size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
        bool match = true;
        for (std::size_t j = 0; j < needle.size(); ++j) {
            if (lower(static_cast<unsigned char>(haystack[i + j])) !=
                lower(static_cast<unsigned char>(needle[j]))) {
                match = false;
                break;
            }
        }
        if (match) {
            return true;
        }
    }
    return false;
}

}  // namespace

bool Focus::install() {
    auto* dpy = x11_.ctrl();
    active_atom_ = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
    XSelectInput(dpy, DefaultRootWindow(dpy), PropertyChangeMask);
    refresh();
    return true;
}

void Focus::refresh() {
    auto* dpy = x11_.ctrl();
    Atom actual = None;
    int format = 0;
    unsigned long count = 0;
    unsigned long after = 0;
    unsigned char* prop = nullptr;
    const int status = XGetWindowProperty(dpy, DefaultRootWindow(dpy), active_atom_, 0, 1, False,
                                          XA_WINDOW, &actual, &format, &count, &after, &prop);
    Window window = None;
    if (status == Success && prop != nullptr && count > 0) {
        window = *reinterpret_cast<Window*>(prop);
    }
    if (prop != nullptr) {
        XFree(prop);
    }
    wm_class_.clear();
    wm_name_.clear();
    if (window == None) {
        return;
    }
    XClassHint hint{};
    if (XGetClassHint(dpy, window, &hint) == 0) {
        return;
    }
    if (hint.res_class != nullptr) {
        wm_class_ = hint.res_class;
        XFree(hint.res_class);
    }
    if (hint.res_name != nullptr) {
        wm_name_ = hint.res_name;
        XFree(hint.res_name);
    }
}

void Focus::drain() {
    auto* dpy = x11_.ctrl();
    while (XPending(dpy) > 0) {
        XEvent event;
        XNextEvent(dpy, &event);
        if (event.type == PropertyNotify && event.xproperty.atom == active_atom_) {
            refresh();
        }
    }
}

bool Focus::excluded(const std::vector<std::string>& names) const {
    for (const auto& name : names) {
        if (contains_ci(wm_class_, name) || contains_ci(wm_name_, name)) {
            return true;
        }
    }
    return false;
}

}  // namespace layoutfix

#include "x11/clipboard.hpp"

#include "log.hpp"

#include <X11/Xatom.h>
#include <X11/Xlib.h>

#include <chrono>
#include <poll.h>

namespace layoutfix {
namespace {

constexpr long kMaxPropertyLongs = 262144;  // ~1 MiB

}  // namespace

Clipboard::~Clipboard() {
    auto* dpy = x11_.ctrl();
    if (dpy != nullptr && window_ != 0) {
        if (owning_ && XGetSelectionOwner(dpy, clipboard_atom_) == window_) {
            XSetSelectionOwner(dpy, clipboard_atom_, None, CurrentTime);
        }
        XDestroyWindow(dpy, window_);
        XFlush(dpy);
        window_ = 0;
    }
}

bool Clipboard::init() {
    auto* dpy = x11_.ctrl();
    if (dpy == nullptr) {
        return false;
    }
    window_ = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy), -10, -10, 1, 1,
                                  0, 0, 0);
    if (window_ == 0) {
        log::error("не вдалося створити службове вікно буфера обміну");
        return false;
    }
    clipboard_atom_ = XInternAtom(dpy, "CLIPBOARD", False);
    utf8_atom_ = XInternAtom(dpy, "UTF8_STRING", False);
    targets_atom_ = XInternAtom(dpy, "TARGETS", False);
    text_atom_ = XInternAtom(dpy, "TEXT", False);
    prop_atom_ = XInternAtom(dpy, "LAYOUT_FIX_CLIP", False);
    incr_atom_ = XInternAtom(dpy, "INCR", False);
    return true;
}

void Clipboard::handle_event(const XEvent& event) {
    auto* dpy = x11_.ctrl();
    if (dpy == nullptr || window_ == 0) {
        return;
    }
    if (event.type == SelectionClear) {
        if (event.xselectionclear.window == window_ &&
            event.xselectionclear.selection == clipboard_atom_) {
            owning_ = false;
            sentinel_ = false;
            owner_changed_ = true;
            owned_text_.clear();
        }
        return;
    }
    if (event.type != SelectionRequest) {
        return;
    }
    const auto& req = event.xselectionrequest;
    if (req.owner != window_ || req.selection != clipboard_atom_) {
        return;
    }

    XEvent reply{};
    reply.xselection.type = SelectionNotify;
    reply.xselection.display = req.display;
    reply.xselection.requestor = req.requestor;
    reply.xselection.selection = req.selection;
    reply.xselection.target = req.target;
    reply.xselection.time = req.time;
    reply.xselection.property = None;

    const Atom dest_prop =
        req.property != None ? req.property : req.target;

    if (owning_ && !sentinel_) {
        if (req.target == targets_atom_) {
            const Atom supported[] = {targets_atom_, utf8_atom_, text_atom_,
                                      XA_STRING};
            XChangeProperty(dpy, req.requestor, dest_prop, XA_ATOM, 32,
                            PropModeReplace,
                            reinterpret_cast<const unsigned char*>(supported),
                            static_cast<int>(sizeof(supported) / sizeof(Atom)));
            reply.xselection.property = dest_prop;
        } else if (req.target == utf8_atom_ || req.target == text_atom_ ||
                   req.target == XA_STRING) {
            XChangeProperty(
                dpy, req.requestor, dest_prop, req.target, 8, PropModeReplace,
                reinterpret_cast<const unsigned char*>(owned_text_.data()),
                static_cast<int>(owned_text_.size()));
            reply.xselection.property = dest_prop;
            content_served_ = true;
        }
    }

    XSendEvent(dpy, req.requestor, False, 0, &reply);
    XFlush(dpy);
}

void Clipboard::pump_requests() {
    auto* dpy = x11_.ctrl();
    if (dpy == nullptr || window_ == 0) {
        return;
    }
    XEvent ev;
    while (XCheckTypedWindowEvent(dpy, window_, SelectionRequest, &ev)) {
        handle_event(ev);
    }
    while (XCheckTypedWindowEvent(dpy, window_, SelectionClear, &ev)) {
        handle_event(ev);
    }
}

Clipboard::ReadStatus Clipboard::read_target(unsigned long target_atom,
                                             int timeout_ms, std::string& out) {
    auto* dpy = x11_.ctrl();
    if (dpy == nullptr || window_ == 0) {
        return ReadStatus::Rejected;
    }
    XDeleteProperty(dpy, window_, prop_atom_);
    XConvertSelection(dpy, clipboard_atom_, target_atom, prop_atom_, window_,
                      CurrentTime);
    XFlush(dpy);

    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeout_ms);
    while (true) {
        XEventsQueued(dpy, QueuedAfterReading);
        pump_requests();

        XEvent ev;
        if (XCheckTypedWindowEvent(dpy, window_, SelectionNotify, &ev)) {
            if (ev.xselection.selection != clipboard_atom_) {
                continue;
            }
            if (ev.xselection.property == None) {
                return ReadStatus::Rejected;
            }
            Atom actual_type = None;
            int actual_format = 0;
            unsigned long item_count = 0;
            unsigned long bytes_after = 0;
            unsigned char* data = nullptr;
            const int status = XGetWindowProperty(
                dpy, window_, prop_atom_, 0, kMaxPropertyLongs, True,
                AnyPropertyType, &actual_type, &actual_format, &item_count,
                &bytes_after, &data);
            if (status != Success || actual_type == incr_atom_) {
                if (data != nullptr) {
                    XFree(data);
                }
                return ReadStatus::Rejected;
            }
            if (data != nullptr && actual_format == 8 && item_count > 0) {
                out.assign(reinterpret_cast<const char*>(data), item_count);
            } else {
                out.clear();
            }
            if (data != nullptr) {
                XFree(data);
            }
            return ReadStatus::Ok;
        }

        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                              deadline - std::chrono::steady_clock::now())
                              .count();
        if (left <= 0) {
            return ReadStatus::Timeout;
        }
        pollfd pfd{x11_.ctrl_fd(), POLLIN, 0};
        if (poll(&pfd, 1, static_cast<int>(left)) <= 0) {
            return ReadStatus::Timeout;
        }
    }
}

bool Clipboard::read(int timeout_ms, std::string& out) {
    out.clear();
    auto* dpy = x11_.ctrl();
    if (dpy == nullptr || window_ == 0) {
        return false;
    }
    const Window owner = XGetSelectionOwner(dpy, clipboard_atom_);
    if (owner == None) {
        return false;
    }
    if (owner == window_) {
        if (!owning_ || sentinel_) {
            return false;
        }
        out = owned_text_;
        return true;
    }
    const auto utf8_status = read_target(utf8_atom_, timeout_ms, out);
    if (utf8_status == ReadStatus::Ok) {
        return true;
    }
    if (utf8_status == ReadStatus::Timeout) {
        return false;
    }
    return read_target(XA_STRING, timeout_ms, out) == ReadStatus::Ok;
}

bool Clipboard::claim_sentinel() {
    auto* dpy = x11_.ctrl();
    if (dpy == nullptr || window_ == 0) {
        return false;
    }
    // Зчитуємо можливі застарілі SelectionClear перед захопленням.
    XEventsQueued(dpy, QueuedAfterReading);
    pump_requests();
    owned_text_.clear();
    owning_ = true;
    sentinel_ = true;
    owner_changed_ = false;
    content_served_ = false;
    XSetSelectionOwner(dpy, clipboard_atom_, window_, CurrentTime);
    XFlush(dpy);
    return XGetSelectionOwner(dpy, clipboard_atom_) == window_;
}

bool Clipboard::wait_for_owner_change(int timeout_ms) {
    auto* dpy = x11_.ctrl();
    if (dpy == nullptr || window_ == 0) {
        return false;
    }
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeout_ms);
    while (!owner_changed_) {
        XEventsQueued(dpy, QueuedAfterReading);
        pump_requests();
        if (owner_changed_) {
            return true;
        }
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                              deadline - std::chrono::steady_clock::now())
                              .count();
        if (left <= 0) {
            return false;
        }
        pollfd pfd{x11_.ctrl_fd(), POLLIN, 0};
        if (poll(&pfd, 1, static_cast<int>(left)) <= 0) {
            break;
        }
    }
    return owner_changed_;
}

bool Clipboard::set(std::string text) {
    auto* dpy = x11_.ctrl();
    if (dpy == nullptr || window_ == 0) {
        return false;
    }
    XEventsQueued(dpy, QueuedAfterReading);
    pump_requests();
    owned_text_ = std::move(text);
    owning_ = true;
    sentinel_ = false;
    owner_changed_ = false;
    content_served_ = false;
    XSetSelectionOwner(dpy, clipboard_atom_, window_, CurrentTime);
    XFlush(dpy);
    return XGetSelectionOwner(dpy, clipboard_atom_) == window_;
}

void Clipboard::clear() {
    auto* dpy = x11_.ctrl();
    owned_text_.clear();
    owning_ = false;
    sentinel_ = false;
    owner_changed_ = false;
    content_served_ = false;
    if (dpy != nullptr && window_ != 0 &&
        XGetSelectionOwner(dpy, clipboard_atom_) == window_) {
        XSetSelectionOwner(dpy, clipboard_atom_, None, CurrentTime);
        XFlush(dpy);
    }
}

}  // namespace layoutfix

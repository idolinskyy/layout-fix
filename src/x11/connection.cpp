#include "x11/connection.hpp"

#include "log.hpp"

#include <X11/Xlib.h>

#include <string>

namespace layoutfix {
namespace {

int ignore_x_error(Display*, XErrorEvent* event) {
    log::error("X11 код " + std::to_string(event->error_code) + " запит " +
               std::to_string(event->request_code));
    return 0;
}

}  // namespace

XConnection::XConnection() = default;

XConnection::~XConnection() {
    if (data_ != nullptr) {
        XCloseDisplay(data_);
    }
    if (ctrl_ != nullptr) {
        XCloseDisplay(ctrl_);
    }
}

bool XConnection::open() {
    XInitThreads();
    XSetErrorHandler(ignore_x_error);
    ctrl_ = XOpenDisplay(nullptr);
    data_ = XOpenDisplay(nullptr);
    if (ctrl_ == nullptr || data_ == nullptr) {
        log::error("немає з'єднання з X11. Потрібна сесія X-Cinnamon, не Wayland");
        return false;
    }
    return true;
}

int XConnection::ctrl_fd() const { return ConnectionNumber(ctrl_); }
int XConnection::data_fd() const { return ConnectionNumber(data_); }

void XConnection::flush_ctrl() const {
    if (ctrl_ != nullptr) {
        XFlush(ctrl_);
    }
}

}  // namespace layoutfix

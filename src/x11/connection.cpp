#include "x11/connection.hpp"

#include "log.hpp"

#include <X11/Xlib.h>

#include <string>
#include <sys/socket.h>
#include <unistd.h>

namespace layoutfix {
namespace {

int ignore_x_error(Display *, XErrorEvent *event) {
  log::error("X11 код " + std::to_string(event->error_code) + " запит " +
             std::to_string(event->request_code));
  return 0;
}

} // namespace

XConnection::XConnection() = default;

XConnection::~XConnection() {
  if (abandon_) {
    // Сокет закривається без XSync. Сервер прибирає клієнта сам.
    auto drop = [](Display *dpy) {
      if (dpy == nullptr) {
        return;
      }
      const int fd = ConnectionNumber(dpy);
      if (fd >= 0) {
        ::shutdown(fd, SHUT_RDWR);
        ::close(fd);
      }
    };
    drop(data_);
    drop(ctrl_);
    data_ = nullptr;
    ctrl_ = nullptr;
    return;
  }
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

void XConnection::abandon_close() { abandon_ = true; }

} // namespace layoutfix

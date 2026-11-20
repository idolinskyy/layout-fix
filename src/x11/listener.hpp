#pragma once

#include "events.hpp"
#include "x11/connection.hpp"

#include <X11/Xlib.h>
#include <X11/extensions/record.h>

#include <vector>

namespace layoutfix {

class Listener {
public:
    explicit Listener(XConnection& x11) : x11_(x11) {}
    ~Listener();
    Listener(const Listener&) = delete;
    Listener& operator=(const Listener&) = delete;

    bool start();
    void drain();
    std::vector<RawEvent> take();

private:
    static void callback(XPointer closure, XRecordInterceptData* data);

    XConnection& x11_;
    unsigned long context_ = 0;
    bool started_ = false;
    std::vector<RawEvent> queue_;
};

}  // namespace layoutfix

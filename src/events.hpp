#pragma once

namespace layoutfix {

struct RawEvent {
    enum class Type { Press, Release, Button };
    Type type = Type::Press;
    unsigned keycode = 0;
    unsigned state = 0;
};

}  // namespace layoutfix

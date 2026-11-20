#pragma once

#include <vector>

namespace layoutfix {

struct EchoKey {
    unsigned keycode = 0;
    bool press = true;
};

enum class EchoConsume { Matched, Ignored, Done };

// Забирає одну очікувану клавішу з голови черги. Чужі події чергу не зсувають.
inline EchoConsume consume_echo(std::vector<EchoKey>& pending, bool button, bool press,
                                unsigned keycode) {
    if (pending.empty()) {
        return EchoConsume::Done;
    }
    if (button) {
        return EchoConsume::Ignored;
    }
    const EchoKey& expected = pending.front();
    if (expected.keycode == keycode && expected.press == press) {
        pending.erase(pending.begin());
        return pending.empty() ? EchoConsume::Done : EchoConsume::Matched;
    }
    return EchoConsume::Ignored;
}

}  // namespace layoutfix

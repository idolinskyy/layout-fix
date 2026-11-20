#pragma once

#include <vector>

namespace layoutfix {

struct EchoKey {
    unsigned keycode = 0;
    bool press = true;
    bool optional = false;
};

enum class EchoConsume { Matched, Ignored, Done };

// Забирає одну очікувану клавішу з голови черги. Чужі події чергу не зсувають.
// Попередні optional-події (наприклад, пре-відпускання фізично затиснутої
// клавіші) можуть бути пропущені, якщо користувач відпустив клавішу раніше.
inline EchoConsume consume_echo(std::vector<EchoKey>& pending, bool button, bool press,
                                unsigned keycode) {
    if (pending.empty()) {
        return EchoConsume::Done;
    }
    if (button) {
        return EchoConsume::Ignored;
    }
    for (std::size_t i = 0; i < pending.size(); ++i) {
        const EchoKey& expected = pending[i];
        if (expected.keycode == keycode && expected.press == press) {
            pending.erase(pending.begin(), pending.begin() + static_cast<std::ptrdiff_t>(i + 1));
            bool only_optional_left = true;
            for (const auto& rest : pending) {
                if (!rest.optional) {
                    only_optional_left = false;
                    break;
                }
            }
            if (only_optional_left) {
                pending.clear();
            }
            return pending.empty() ? EchoConsume::Done : EchoConsume::Matched;
        }
        if (!expected.optional) {
            break;
        }
    }
    return EchoConsume::Ignored;
}

}  // namespace layoutfix

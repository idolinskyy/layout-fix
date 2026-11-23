#pragma once

namespace layoutfix {

struct SwitchAttempt {
    bool activate_ok = false;
    bool saw_target = false;
    bool rollback_ok = false;
    bool saw_fallback = false;
};

enum class SwitchOutcome { Applied, Restored, Desynced };

// Applied: XKB уже в цільовій мові.
// Restored: перемикання не сталося або вдалося повернути попередню мову.
// Desynced: Cinnamon змінив джерело, а XKB не збігся ні з ціллю, ні з відкатом.
inline SwitchOutcome resolve_switch(const SwitchAttempt& attempt) {
    if (attempt.saw_target) {
        return SwitchOutcome::Applied;
    }
    if (!attempt.activate_ok || attempt.saw_fallback) {
        return SwitchOutcome::Restored;
    }
    return SwitchOutcome::Desynced;
}

}  // namespace layoutfix

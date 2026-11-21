#pragma once

namespace layoutfix {

enum class TrayMode { Active, Paused, Desynced, PausedDesynced };

inline TrayMode choose_tray_mode(bool paused, bool maps_ready) {
    if (paused) {
        return maps_ready ? TrayMode::Paused : TrayMode::PausedDesynced;
    }
    return maps_ready ? TrayMode::Active : TrayMode::Desynced;
}

}  // namespace layoutfix


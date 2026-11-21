#pragma once

#include "config.hpp"
#include "lang.hpp"
#include "tray_state.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace layoutfix {

struct TrayLanguageOption {
    Lang lang = Lang::Other;
    int source_index = -1;
};

struct TrayCallbacks {
    std::function<void()> on_toggle_pause;
    std::function<void(int)> on_activate_source;
    std::function<void()> on_quit;
    std::function<void(bool entering)> on_modal_pause;
    std::function<void()> on_after_modal;
    std::function<const Config&()> get_config;
    std::function<std::string()> get_last_window;
    std::function<bool(const Config&)> validate_hotkeys;
    std::function<void(Config)> apply_config;
};

class Tray {
public:
    explicit Tray(TrayCallbacks callbacks);
    ~Tray();
    Tray(const Tray&) = delete;
    Tray& operator=(const Tray&) = delete;

    void set_languages(const std::vector<TrayLanguageOption>& options);
    void update(Lang active_language, TrayMode mode, bool paused,
                bool force = false);
    void process_events();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace layoutfix


#pragma once

#include <string>
#include <vector>

namespace layoutfix {

struct Config {
    int min_length = 3;
    std::string dict_en;
    std::string dict_uk;
    std::vector<std::string> exclude{"gnome-terminal", "terminator", "xterm"};
    std::string pause_key = "Pause";
    std::string undo_key = "Shift+Pause";
    bool debug_keys = false;
};

struct Hotkey {
    std::string keysym = "Pause";
    bool shift = false;
};

Hotkey parse_hotkey(std::string_view text);
Config load_config(const std::string& path, bool debug_keys);
std::string default_config_path();

}  // namespace layoutfix

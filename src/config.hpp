#pragma once

#include <string>
#include <unordered_set>
#include <vector>

namespace layoutfix {

struct Config {
  std::string config_path;
  std::string user_config_path;
  bool config_path_explicit = false;
  int min_length = 3;
  std::string dict_en;
  std::string dict_uk;
  std::string dict_ru;
  std::vector<std::string> exclude{"gnome-terminal",
                                   "terminator",
                                   "xterm",
                                   "alacritty",
                                   "kitty",
                                   "konsole",
                                   "tilix",
                                   "xfce4-terminal",
                                   "wezterm",
                                   "urxvt",
                                   "mate-terminal",
                                   "cinnamon-screensaver",
                                   "org.cinnamon.ScreenSaver",
                                   "gcr-prompter",
                                   "keepassxc",
                                   "bitwarden",
                                   "1password",
                                   "layout-fix"};
  std::vector<std::string> ignore_words;
  std::string pause_key = "Pause";
  std::string undo_key = "Shift+Pause";
  std::string convert_key = "Alt+Pause";
  bool debug_keys = false;
};

struct Hotkey {
  std::string keysym = "Pause";
  bool shift = false;
  bool ctrl = false;
  bool alt = false;
  bool super = false;
};

Hotkey parse_hotkey(std::string_view text);
std::string hotkey_from_qt_sequence(std::string_view qt_portable);
std::string hotkey_to_qt_sequence(std::string_view config_hotkey);
Config load_config(const std::string &path, bool debug_keys);
std::string default_config_path();
std::string default_user_config_path();
bool save_config_file(const Config &config, const std::string &path);
std::unordered_set<std::string> build_ignore_set(
    const std::vector<std::string> &words);

} // namespace layoutfix

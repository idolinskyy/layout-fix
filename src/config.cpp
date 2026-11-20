#include "config.hpp"

#include "log.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <exception>

namespace layoutfix {
namespace {

std::string trim(std::string_view text) {
    std::size_t begin = 0;
    while (begin < text.size() &&
           std::isspace(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

std::vector<std::string> split_csv(std::string_view text) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto comma = text.find(',', start);
        const auto piece = text.substr(start, comma == std::string_view::npos
                                                   ? std::string_view::npos
                                                   : comma - start);
        auto item = trim(piece);
        if (!item.empty()) {
            parts.push_back(std::move(item));
        }
        if (comma == std::string_view::npos) {
            break;
        }
        start = comma + 1;
    }
    return parts;
}

std::string expand_home(std::string path) {
    if (path == "~" || path.rfind("~/", 0) == 0) {
        const char* home = std::getenv("HOME");
        if (home != nullptr) {
            path.replace(0, 1, home);
        }
    }
    return path;
}

std::string to_lower_ascii(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (char ch : text) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return out;
}

}  // namespace

Hotkey parse_hotkey(std::string_view text) {
    Hotkey key;
    key.keysym.clear();
    const auto body = trim(text);
    if (body.empty()) {
        return key;
    }

    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start <= body.size()) {
        const auto plus = body.find('+', start);
        const auto piece = body.substr(start, plus == std::string::npos
                                                  ? std::string::npos
                                                  : plus - start);
        auto item = trim(piece);
        if (item.empty()) {
            return key;
        }
        parts.push_back(std::move(item));
        if (plus == std::string::npos) {
            break;
        }
        start = plus + 1;
    }

    for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
        const auto mod = to_lower_ascii(parts[i]);
        if (mod == "shift") {
            key.shift = true;
        } else if (mod == "ctrl" || mod == "control") {
            key.ctrl = true;
        } else if (mod == "alt" || mod == "mod1") {
            key.alt = true;
        } else if (mod == "super" || mod == "win" || mod == "meta") {
            key.super = true;
        } else {
            return key;
        }
    }
    key.keysym = parts.back();
    return key;
}

std::string default_user_config_path() {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    std::filesystem::path dir = xdg != nullptr && xdg[0] != '\0'
                                    ? std::filesystem::path(xdg)
                                    : std::filesystem::path(std::getenv("HOME") ? std::getenv("HOME") : "") /
                                          ".config";
    return (dir / "layout-fix" / "layout-fix.conf").string();
}

std::string default_config_path() {
    const std::filesystem::path user_config = default_user_config_path();
    if (std::filesystem::is_regular_file(user_config)) {
        return user_config.string();
    }

    const char* system_dirs = std::getenv("XDG_CONFIG_DIRS");
    std::string_view search_dirs =
        system_dirs != nullptr && system_dirs[0] != '\0' ? system_dirs
                                                         : "/etc/xdg";
    std::size_t start = 0;
    while (start <= search_dirs.size()) {
        const auto separator = search_dirs.find(':', start);
        const auto item = search_dirs.substr(
            start, separator == std::string_view::npos
                       ? std::string_view::npos
                       : separator - start);
        if (!item.empty()) {
            const auto system_config =
                std::filesystem::path(item) / "layout-fix" / "layout-fix.conf";
            if (std::filesystem::is_regular_file(system_config)) {
                return system_config.string();
            }
        }
        if (separator == std::string_view::npos) {
            break;
        }
        start = separator + 1;
    }
    return user_config.string();
}

Config load_config(const std::string& path, bool debug_keys) {
    Config cfg;
    cfg.config_path = path;
    cfg.user_config_path = default_user_config_path();
    cfg.debug_keys = debug_keys;
    if (path.empty() || !std::filesystem::exists(path)) {
        if (!path.empty()) {
            log::info("конфіг не знайдено, беру значення за замовчуванням");
        }
        return cfg;
    }

    std::ifstream in(path);
    if (!in) {
        log::error("не вдалося прочитати конфіг " + path);
        return cfg;
    }

    std::string line;
    while (std::getline(in, line)) {
        const auto hash = line.find('#');
        if (hash != std::string::npos) {
            line.resize(hash);
        }
        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const auto key = trim(std::string_view(line).substr(0, eq));
        auto value = trim(std::string_view(line).substr(eq + 1));
        if (key == "min-length") {
            try {
                cfg.min_length = std::max(1, std::stoi(value));
            } catch (const std::exception&) {
                log::error("min-length має бути числом");
            }
        } else if (key == "dict-en") {
            cfg.dict_en = expand_home(std::move(value));
        } else if (key == "dict-uk") {
            cfg.dict_uk = expand_home(std::move(value));
        } else if (key == "exclude") {
            cfg.exclude = split_csv(value);
        } else if (key == "pause-key") {
            if (!value.empty()) {
                cfg.pause_key = std::move(value);
            }
        } else if (key == "undo-key") {
            if (!value.empty()) {
                cfg.undo_key = std::move(value);
            }
        } else if (key == "convert-key") {
            if (!value.empty()) {
                cfg.convert_key = std::move(value);
            }
        } else if (!key.empty()) {
            log::info("невідомий параметр конфігу: " + key);
        }
    }
    return cfg;
}

bool save_config_file(const Config &config, const std::string &path) {
    if (path.empty()) {
        return false;
    }
    const std::filesystem::path config_path(path);
    std::error_code error;
    if (!config_path.parent_path().empty()) {
        std::filesystem::create_directories(config_path.parent_path(), error);
        if (error) {
            return false;
        }
    }

    std::ofstream out(config_path, std::ios::trunc);
    if (!out) {
        return false;
    }
    out << "min-length=" << config.min_length << '\n';
    if (!config.dict_en.empty()) {
        out << "dict-en=" << config.dict_en << '\n';
    }
    if (!config.dict_uk.empty()) {
        out << "dict-uk=" << config.dict_uk << '\n';
    }
    out << "exclude=";
    for (std::size_t i = 0; i < config.exclude.size(); ++i) {
        if (i != 0) {
            out << ',';
        }
        out << config.exclude[i];
    }
    out << '\n'
        << "pause-key=" << config.pause_key << '\n'
        << "undo-key=" << config.undo_key << '\n'
        << "convert-key=" << config.convert_key << '\n';
    out.flush();
    return out.good();
}

}  // namespace layoutfix

#include "config.hpp"

#include "log.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>

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

}  // namespace

Hotkey parse_hotkey(std::string_view text) {
    Hotkey key;
    auto body = trim(text);
    if (body.rfind("Shift+", 0) == 0) {
        key.shift = true;
        body = body.substr(6);
    }
    key.keysym = trim(body);
    if (key.keysym.empty()) {
        key.keysym = "Pause";
    }
    return key;
}

std::string default_config_path() {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    std::filesystem::path dir = xdg != nullptr && xdg[0] != '\0'
                                    ? std::filesystem::path(xdg)
                                    : std::filesystem::path(std::getenv("HOME") ? std::getenv("HOME") : "") /
                                          ".config";
    return (dir / "layout-fix" / "layout-fix.conf").string();
}

Config load_config(const std::string& path, bool debug_keys) {
    Config cfg;
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
            cfg.pause_key = std::move(value);
        } else if (key == "undo-key") {
            cfg.undo_key = std::move(value);
        } else if (!key.empty()) {
            log::info("невідомий параметр конфігу: " + key);
        }
    }
    return cfg;
}

}  // namespace layoutfix

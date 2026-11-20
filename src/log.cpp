#include "log.hpp"

#include <iostream>

namespace layoutfix::log {
namespace {
bool g_debug_keys = false;
}

void set_debug_keys(bool enabled) { g_debug_keys = enabled; }
bool debug_keys() { return g_debug_keys; }

void info(std::string_view message) {
    std::cerr << "layout-fix: " << message << '\n';
}

void error(std::string_view message) {
    std::cerr << "layout-fix: помилка: " << message << '\n';
}

void keys(std::string_view message) {
    if (g_debug_keys) {
        std::cerr << "layout-fix: keys: " << message << '\n';
    }
}

}  // namespace layoutfix::log

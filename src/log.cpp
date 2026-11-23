#include "log.hpp"

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace layoutfix::log {
namespace {

bool g_debug_keys = false;
std::ofstream g_file;
std::string g_path;
std::size_t g_max_bytes = kDefaultMaxBytes;
std::size_t g_size = 0;

std::string timestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    std::tm local {};
    localtime_r(&seconds, &local);
    char buffer[32];
    if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &local) == 0) {
        return {};
    }
    return buffer;
}

std::size_t file_size_or_zero(const std::filesystem::path& path) {
    std::error_code error;
    const auto bytes = std::filesystem::file_size(path, error);
    if (error) {
        return 0;
    }
    return static_cast<std::size_t>(bytes);
}

void rotate() {
    g_file.close();
    g_file.clear();
    std::error_code error;
    const auto previous = std::filesystem::path(g_path + ".1");
    std::filesystem::remove(previous, error);
    std::filesystem::rename(g_path, previous, error);
    if (error) {
        std::filesystem::resize_file(g_path, 0, error);
    }
    g_file.open(g_path, std::ios::out | std::ios::app);
    g_size = 0;
}

void write_line(const std::string& line) {
    std::cerr << line << '\n';
    if (!g_file.is_open()) {
        return;
    }
    const std::string stored = timestamp() + " " + line + "\n";
    if (g_size > 0 && g_size + stored.size() > g_max_bytes) {
        rotate();
    }
    if (!g_file.is_open()) {
        return;
    }
    g_file << stored;
    g_file.flush();
    g_size += stored.size();
}

std::string read_file(const std::filesystem::path& path) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) {
        return {};
    }
    std::ifstream input(path);
    if (!input) {
        return {};
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

}  // namespace

void set_debug_keys(bool enabled) { g_debug_keys = enabled; }
bool debug_keys() { return g_debug_keys; }

bool open_file(const std::string& path, std::size_t max_bytes) {
    close_file();
    if (path.empty()) {
        return false;
    }
    std::error_code error;
    const auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, error);
        if (error) {
            return false;
        }
    }
    g_path = path;
    g_max_bytes = max_bytes == 0 ? 1 : max_bytes;
    if (file_size_or_zero(path) >= g_max_bytes) {
        rotate();
    }
    if (!g_file.is_open()) {
        g_file.open(path, std::ios::out | std::ios::app);
    }
    if (!g_file) {
        g_path.clear();
        g_file.close();
        return false;
    }
    g_size = file_size_or_zero(path);
    return true;
}

void close_file() {
    if (g_file.is_open()) {
        g_file.flush();
        g_file.close();
    }
    g_file.clear();
    g_path.clear();
    g_size = 0;
    g_max_bytes = kDefaultMaxBytes;
}

std::string default_path() {
    std::filesystem::path base;
    if (const char* state = std::getenv("XDG_STATE_HOME");
        state != nullptr && state[0] != '\0') {
        base = state;
    } else if (const char* home = std::getenv("HOME");
               home != nullptr && home[0] != '\0') {
        base = std::filesystem::path(home) / ".local" / "state";
    } else {
        return {};
    }
    return (base / "layout-fix" / "layout-fix.log").string();
}

void info(std::string_view message) {
    write_line("layout-fix: " + std::string(message));
}

void error(std::string_view message) {
    write_line("layout-fix: помилка: " + std::string(message));
}

void keys(std::string_view message) {
    if (g_debug_keys) {
        std::cerr << "layout-fix: keys: " << message << '\n';
    }
}

std::string read_text(const std::string& path) {
    if (path.empty()) {
        return {};
    }
    std::string text = read_file(std::filesystem::path(path + ".1"));
    text += read_file(path);
    return text;
}

}  // namespace layoutfix::log

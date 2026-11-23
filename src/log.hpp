#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace layoutfix::log {

inline constexpr std::size_t kDefaultMaxBytes = 256 * 1024;

void set_debug_keys(bool enabled);
bool debug_keys();

// Дописує info/error у файл і лишає ті самі рядки в stderr.
// keys() у файл не потрапляє. Якщо відкрити файл не вдалося, лишається лише stderr.
bool open_file(const std::string& path, std::size_t max_bytes = kDefaultMaxBytes);
void close_file();

std::string default_path();

void info(std::string_view message);
void error(std::string_view message);
void keys(std::string_view message);

// Поточний файл і попередня частина після ротації, від старіших рядків.
std::string read_text(const std::string& path);

}  // namespace layoutfix::log

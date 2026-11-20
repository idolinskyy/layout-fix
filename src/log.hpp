#pragma once

#include <string_view>

namespace layoutfix::log {

void set_debug_keys(bool enabled);
bool debug_keys();
void info(std::string_view message);
void error(std::string_view message);
void keys(std::string_view message);

}  // namespace layoutfix::log

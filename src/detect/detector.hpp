#pragma once

#include "detect/dictionary.hpp"

#include <string_view>

namespace layoutfix {

enum class Decision { Keep, Switch };

Decision decide(std::string_view current, std::string_view other,
                const Dictionary& current_dict, const Dictionary& other_dict,
                int min_length);

}  // namespace layoutfix

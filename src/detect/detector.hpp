#pragma once

#include "detect/dictionary.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace layoutfix {

enum class Decision { Keep, Switch };

Decision decide(std::string_view current, std::string_view other,
                const Dictionary& current_dict, const Dictionary& other_dict,
                int min_length);

Decision decide_context(const std::vector<std::string>& current_words,
                        const std::vector<std::string>& other_words,
                        const Dictionary& current_dict,
                        const Dictionary& other_dict, int min_length);

}  // namespace layoutfix

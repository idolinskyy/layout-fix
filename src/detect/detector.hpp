#pragma once

#include "detect/dictionary.hpp"
#include "lang.hpp"

#include <array>
#include <optional>
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

bool is_ambiguous_word(
    const std::array<std::string, 3>& forms, Lang source_language,
    const std::array<const Dictionary*, 3>& dictionaries);

std::optional<Lang> unique_language_candidate(
    const std::array<std::vector<std::string>, 3>& words_by_language,
    Lang source_language,
    const std::array<const Dictionary*, 3>& dictionaries, int min_length);

std::optional<Lang> choose_forced_language(
    const std::array<std::string, 3>& forms, Lang source_language,
    const std::array<const Dictionary*, 3>& dictionaries,
    const std::array<bool, 3>& active_languages,
    const std::array<bool, 3>& visited_languages);

}  // namespace layoutfix

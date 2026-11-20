#include "detect/detector.hpp"

#include "utf8.hpp"

namespace layoutfix {

Decision decide(std::string_view current, std::string_view other,
                const Dictionary& current_dict, const Dictionary& other_dict,
                int min_length) {
    if (utf8::char_count(current) < static_cast<std::size_t>(min_length)) {
        return Decision::Keep;
    }
    if (current_dict.contains(current)) {
        return Decision::Keep;
    }
    if (!other.empty() && other_dict.contains(other)) {
        return Decision::Switch;
    }
    return Decision::Keep;
}

Decision decide_context(const std::vector<std::string>& current_words,
                        const std::vector<std::string>& other_words,
                        const Dictionary& current_dict,
                        const Dictionary& other_dict, int min_length) {
    const auto minimum = static_cast<std::size_t>(min_length);
    if (current_words.size() < 2 || current_words.size() != other_words.size()) {
        return Decision::Keep;
    }

    bool has_short_word = false;
    bool current_accepts_all = true;
    for (std::size_t i = 0; i < current_words.size(); ++i) {
        if (current_words[i].empty() || other_words[i].empty()) {
            return Decision::Keep;
        }
        has_short_word = has_short_word ||
                         utf8::char_count(current_words[i]) < minimum;
        current_accepts_all = current_accepts_all &&
                              current_dict.contains(current_words[i]);
        if (!other_dict.contains(other_words[i])) {
            return Decision::Keep;
        }
    }
    return has_short_word && !current_accepts_all ? Decision::Switch
                                                   : Decision::Keep;
}

std::optional<Lang> unique_language_candidate(
    const std::array<std::vector<std::string>, 3>& words_by_language,
    Lang source_language,
    const std::array<const Dictionary*, 3>& dictionaries, int min_length) {
    const auto source_index = lang_index(source_language);
    if (source_index >= words_by_language.size()) {
        return std::nullopt;
    }
    const auto word_count = words_by_language[source_index].size();
    if (word_count == 0 || word_count > 3) {
        return std::nullopt;
    }
    for (const auto& words : words_by_language) {
        if (words.size() != word_count) {
            return std::nullopt;
        }
    }

    if (word_count == 1) {
        if (utf8::char_count(words_by_language[source_index][0]) <
            static_cast<std::size_t>(min_length)) {
            return std::nullopt;
        }
    } else {
        bool has_short_word = false;
        for (const auto& word : words_by_language[source_index]) {
            has_short_word = has_short_word ||
                             utf8::char_count(word) <
                                 static_cast<std::size_t>(min_length);
        }
        if (!has_short_word) {
            return std::nullopt;
        }
    }

    std::optional<Lang> match;
    for (std::size_t language = 0; language < dictionaries.size(); ++language) {
        if (dictionaries[language] == nullptr) {
            continue;
        }
        bool accepts_all = true;
        for (const auto& word : words_by_language[language]) {
            if (!dictionaries[language]->contains(word)) {
                accepts_all = false;
                break;
            }
        }
        if (!accepts_all) {
            continue;
        }
        if (match.has_value()) {
            return std::nullopt;
        }
        match = static_cast<Lang>(language);
    }
    if (!match.has_value() || *match == source_language) {
        return std::nullopt;
    }
    return match;
}

}  // namespace layoutfix

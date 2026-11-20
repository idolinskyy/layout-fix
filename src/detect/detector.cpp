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

}  // namespace layoutfix

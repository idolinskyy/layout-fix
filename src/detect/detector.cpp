#include "detect/detector.hpp"

#include "utf8.hpp"

namespace layoutfix {
namespace {

bool is_opening_punct(char ch) {
    return ch == '"' || ch == '\'' || ch == '[' || ch == '{' || ch == '<' ||
           ch == '`';
}

bool is_trailing_punct(char ch) {
    return ch == ',' || ch == '.' || ch == ';' || ch == ':' || ch == '"' ||
           ch == '\'' || ch == ']' || ch == '}' || ch == '>';
}

char matching_close(char open) {
    switch (open) {
        case '[':
            return ']';
        case '{':
            return '}';
        case '<':
            return '>';
        case '"':
            return '"';
        case '\'':
            return '\'';
        case '`':
            return '`';
        default:
            return '\0';
    }
}

bool starts_with_word_char(std::string_view text) {
    if (text.empty()) {
        return false;
    }
    std::size_t i = 0;
    return utf8::is_word_char(utf8::decode_at(text, i));
}

bool ends_with_word_char(std::string_view text) {
    if (text.empty()) {
        return false;
    }
    std::size_t i = 0;
    char32_t last = 0;
    while (i < text.size()) {
        last = utf8::decode_at(text, i);
    }
    return utf8::is_word_char(last);
}

bool word_accepted_by(std::string_view word, const Dictionary& dict,
                      bool is_source, std::size_t min_target_len) {
    if (word.empty()) {
        return false;
    }
    if (dict.contains(word)) {
        return true;
    }

    // 1) Природна кінцева пунктуація після слова (напр. "he,", "hello.", "руддщб" -> "hello,").
    {
        std::string_view core = word;
        while (!core.empty() && is_trailing_punct(core.back())) {
            core.remove_suffix(1);
        }
        if (core.size() < word.size() && !core.empty() &&
            starts_with_word_char(core)) {
            if (is_source) {
                if (dict.contains(core)) {
                    return true;
                }
            } else if (utf8::char_count(core) >= min_target_len &&
                       dict.contains(core)) {
                return true;
            }
        }
    }

    if (!is_source) {
        return false;
    }

    // 2) Відкривальна пунктуація на початку слова вихідної мови (напр. "[hello", "\"hello").
    {
        std::string_view core = word;
        while (!core.empty() && is_opening_punct(core.front())) {
            core.remove_prefix(1);
        }
        if (core.size() < word.size() && !core.empty() &&
            ends_with_word_char(core) && dict.contains(core)) {
            return true;
        }
    }

    // 3) Парне обрамлення + необов'язкова кінцева пунктуація (напр. "[hello]", "\"hello\",").
    {
        std::string_view inner = word;
        while (!inner.empty() &&
               (inner.back() == ',' || inner.back() == '.' ||
                inner.back() == ';' || inner.back() == ':')) {
            inner.remove_suffix(1);
        }
        bool unwrapped = false;
        while (inner.size() >= 2 &&
               matching_close(inner.front()) == inner.back()) {
            inner.remove_prefix(1);
            inner.remove_suffix(1);
            unwrapped = true;
        }
        if (unwrapped && !inner.empty() && dict.contains(inner)) {
            return true;
        }
    }

    return false;
}

}  // namespace

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

    const std::size_t min_target_len =
        word_count == 1 ? static_cast<std::size_t>(min_length) : 1;

    std::optional<Lang> match;
    for (std::size_t language = 0; language < dictionaries.size(); ++language) {
        if (dictionaries[language] == nullptr) {
            continue;
        }
        const bool is_source = language == source_index;
        bool accepts_all = true;
        for (const auto& word : words_by_language[language]) {
            if (!word_accepted_by(word, *dictionaries[language], is_source,
                                  min_target_len)) {
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

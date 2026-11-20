#include "detect/selection.hpp"

#include "utf8.hpp"

#include <cstddef>
#include <vector>

namespace layoutfix {
namespace {

bool is_latin(char32_t cp) {
    return (cp >= U'A' && cp <= U'Z') || (cp >= U'a' && cp <= U'z');
}

bool is_cyrillic(char32_t cp) {
    return cp >= 0x0400 && cp <= 0x04FF;
}

bool is_uk_specific(char32_t cp) {
    return cp == 0x0404 || cp == 0x0454 ||  // Є є
           cp == 0x0406 || cp == 0x0456 ||  // І і
           cp == 0x0407 || cp == 0x0457 ||  // Ї ї
           cp == 0x0490 || cp == 0x0491;    // Ґ ґ
}

bool is_ru_specific(char32_t cp) {
    return cp == 0x0401 || cp == 0x0451 ||  // Ё ё
           cp == 0x042A || cp == 0x044A ||  // Ъ ъ
           cp == 0x042B || cp == 0x044B ||  // Ы ы
           cp == 0x042D || cp == 0x044D;    // Э э
}

bool is_edge_punctuation(char32_t cp) {
    return cp == U'\'' || cp == U'-' || cp == 0x2019 || cp == 0x02BC;
}

bool is_cyrillic_lang(Lang lang) {
    return lang == Lang::Uk || lang == Lang::Ru;
}

std::vector<std::string> extract_words(std::string_view text) {
    std::vector<std::string> words;
    std::size_t i = 0;
    while (i < text.size()) {
        std::size_t token_start = i;
        while (i < text.size()) {
            const std::size_t prev = i;
            const char32_t cp = utf8::decode_at(text, i);
            if (!utf8::is_word_char(cp)) {
                i = prev;
                break;
            }
        }
        if (i > token_start) {
            std::string_view token = text.substr(token_start, i - token_start);
            // Trim leading edge punctuation (' or -)
            std::size_t lead = 0;
            while (lead < token.size()) {
                std::size_t next = lead;
                const char32_t cp = utf8::decode_at(token, next);
                if (!is_edge_punctuation(cp)) {
                    break;
                }
                lead = next;
            }
            token.remove_prefix(lead);
            // Trim trailing edge punctuation
            while (!token.empty()) {
                std::size_t last_start = 0;
                std::size_t scan = 0;
                char32_t last_cp = 0;
                while (scan < token.size()) {
                    last_start = scan;
                    last_cp = utf8::decode_at(token, scan);
                }
                if (!is_edge_punctuation(last_cp)) {
                    break;
                }
                token = token.substr(0, last_start);
            }
            bool has_letter = false;
            std::size_t check = 0;
            while (check < token.size()) {
                if (utf8::is_letter(utf8::decode_at(token, check))) {
                    has_letter = true;
                    break;
                }
            }
            if (has_letter) {
                words.emplace_back(token);
            }
        }
        if (i < text.size()) {
            utf8::decode_at(text, i);
        }
    }
    return words;
}

std::size_t score_words(const std::vector<std::string>& words,
                        const Dictionary* dict) {
    if (dict == nullptr) {
        return 0;
    }
    std::size_t hits = 0;
    for (const auto& word : words) {
        if (dict->contains(word)) {
            ++hits;
        }
    }
    return hits;
}

}  // namespace

std::string map_layout_text(std::string_view text, const LayoutMaps& maps,
                            Lang from, Lang to) {
    if (from == Lang::Other || to == Lang::Other || from == to) {
        return std::string(text);
    }
    const auto& table = maps.tables[lang_index(from)][lang_index(to)];
    std::string out;
    out.reserve(text.size() * 2);
    std::size_t i = 0;
    while (i < text.size()) {
        const std::size_t start = i;
        utf8::decode_at(text, i);
        const std::string key(text.substr(start, i - start));
        const auto it = table.find(key);
        if (it != table.end()) {
            out += it->second;
        } else {
            out += key;
        }
    }
    return out;
}

SelectionResult convert_selection(
    std::string_view text, const LayoutMaps& maps,
    const std::array<const Dictionary*, 3>& dictionaries,
    const std::array<bool, 3>& active_languages, Lang current_language) {
    SelectionResult none;
    if (text.empty()) {
        return none;
    }

    std::size_t latin_count = 0;
    std::size_t cyrillic_count = 0;
    std::size_t uk_specific = 0;
    std::size_t ru_specific = 0;
    std::size_t pos = 0;
    while (pos < text.size()) {
        const char32_t cp = utf8::decode_at(text, pos);
        if (is_latin(cp)) {
            ++latin_count;
        } else if (is_cyrillic(cp)) {
            ++cyrillic_count;
            if (is_uk_specific(cp)) {
                ++uk_specific;
            } else if (is_ru_specific(cp)) {
                ++ru_specific;
            }
        }
    }

    std::vector<Lang> sources;
    if (latin_count == 0 && cyrillic_count == 0) {
        if (current_language != Lang::Other &&
            active_languages[lang_index(current_language)]) {
            sources.push_back(current_language);
        } else {
            for (const Lang lang : kSupportedLanguages) {
                if (active_languages[lang_index(lang)]) {
                    sources.push_back(lang);
                }
            }
        }
    } else if (latin_count > cyrillic_count) {
        if (active_languages[lang_index(Lang::En)]) {
            sources.push_back(Lang::En);
        }
    } else {
        if (uk_specific > 0 && ru_specific == 0 &&
            active_languages[lang_index(Lang::Uk)]) {
            sources.push_back(Lang::Uk);
        } else if (ru_specific > 0 && uk_specific == 0 &&
                   active_languages[lang_index(Lang::Ru)]) {
            sources.push_back(Lang::Ru);
        } else {
            if (active_languages[lang_index(Lang::Uk)]) {
                sources.push_back(Lang::Uk);
            }
            if (active_languages[lang_index(Lang::Ru)]) {
                sources.push_back(Lang::Ru);
            }
        }
    }

    struct Candidate {
        Lang source = Lang::Other;
        Lang target = Lang::Other;
        std::string mapped;
        std::size_t score = 0;
        bool cross_script = false;
    };

    std::vector<Candidate> candidates;
    for (const Lang source : sources) {
        for (const Lang target : kSupportedLanguages) {
            if (target == source || !active_languages[lang_index(target)]) {
                continue;
            }
            std::string mapped = map_layout_text(text, maps, source, target);
            if (mapped == text) {
                continue;
            }
            const auto words = extract_words(mapped);
            const std::size_t score =
                score_words(words, dictionaries[lang_index(target)]);
            const bool cross = is_cyrillic_lang(source) != is_cyrillic_lang(target);
            candidates.push_back(Candidate{source, target, std::move(mapped),
                                           score, cross});
        }
    }
    if (candidates.empty()) {
        return none;
    }

    auto preference_rank = [&](const Candidate& c) -> int {
        // Higher is better when dictionary scores tie.
        int rank = 0;
        if (c.cross_script) {
            rank += 100;
        }
        if (c.target == current_language) {
            rank += 20;
        }
        if (c.target == Lang::Uk) {
            rank += 10;
        } else if (c.target == Lang::En) {
            rank += 5;
        }
        return rank;
    };

    const Candidate* best = &candidates[0];
    for (std::size_t i = 1; i < candidates.size(); ++i) {
        const Candidate& cand = candidates[i];
        if (cand.score > best->score ||
            (cand.score == best->score &&
             preference_rank(cand) > preference_rank(*best))) {
            best = &cand;
        }
    }

    return SelectionResult{best->mapped, best->target, true};
}

}  // namespace layoutfix


#pragma once

#include <cstddef>
#ifdef LAYOUT_FIX_HAVE_HUNSPELL
#include <hunspell/hunspell.hxx>
#include <memory>
#endif
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace layoutfix {

class Dictionary {
public:
    ~Dictionary();
    bool load_file(const std::string& path);
    bool load_text(std::string_view text);
    bool contains(std::string_view word) const;
    bool has_morphology() const;
    std::size_t size() const { return words_.size(); }

private:
    void add_line(std::string_view line, bool& skip_count);
    std::unordered_set<std::string> words_;
#ifdef LAYOUT_FIX_HAVE_HUNSPELL
    std::unique_ptr<Hunspell> hunspell_;
#endif
};

// Перший існуючий файл зі списку. Порожній рядок, якщо нічого немає.
std::string resolve_dictionary(const std::string& configured,
                               const std::vector<std::string>& fallbacks);

}  // namespace layoutfix

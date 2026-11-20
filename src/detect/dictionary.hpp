#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace layoutfix {

class Dictionary {
public:
    bool load_file(const std::string& path);
    bool load_text(std::string_view text);
    bool contains(std::string_view word) const;
    std::size_t size() const { return words_.size(); }

private:
    void add_line(std::string_view line, bool& skip_count);
    std::unordered_set<std::string> words_;
};

// Перший існуючий файл зі списку. Порожній рядок, якщо нічого немає.
std::string resolve_dictionary(const std::string& configured,
                               const std::vector<std::string>& fallbacks);

}  // namespace layoutfix

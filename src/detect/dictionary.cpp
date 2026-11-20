#include "detect/dictionary.hpp"

#include "utf8.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace layoutfix {
namespace {

std::string_view stem_of(std::string_view line) {
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
        line.remove_suffix(1);
    }
    const auto cut = line.find_first_of("/\t ");
    if (cut != std::string_view::npos) {
        line = line.substr(0, cut);
    }
    return line;
}

bool all_digits(std::string_view text) {
    if (text.empty()) {
        return false;
    }
    for (char ch : text) {
        if (ch < '0' || ch > '9') {
            return false;
        }
    }
    return true;
}

}  // namespace

void Dictionary::add_line(std::string_view line, bool& skip_count) {
    const auto stem = stem_of(line);
    if (stem.empty()) {
        return;
    }
    if (skip_count && all_digits(stem)) {
        skip_count = false;
        words_.reserve(static_cast<std::size_t>(std::stoul(std::string(stem))));
        return;
    }
    skip_count = false;
    words_.insert(utf8::casefold(stem));
}

bool Dictionary::load_text(std::string_view text) {
    bool skip_count = true;
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto nl = text.find('\n', start);
        const auto line = text.substr(start, nl == std::string_view::npos ? std::string_view::npos
                                                                          : nl - start);
        add_line(line, skip_count);
        if (nl == std::string_view::npos) {
            break;
        }
        start = nl + 1;
    }
    return !words_.empty();
}

bool Dictionary::load_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        return false;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return load_text(buffer.str());
}

bool Dictionary::contains(std::string_view word) const {
    if (word.empty()) {
        return false;
    }
    return words_.contains(utf8::casefold(word));
}

std::string resolve_dictionary(const std::string& configured,
                               const std::vector<std::string>& fallbacks) {
    namespace fs = std::filesystem;
    if (!configured.empty() && fs::is_regular_file(configured)) {
        return configured;
    }
    for (const auto& path : fallbacks) {
        if (!path.empty() && fs::is_regular_file(path)) {
            return path;
        }
    }
    return {};
}

}  // namespace layoutfix

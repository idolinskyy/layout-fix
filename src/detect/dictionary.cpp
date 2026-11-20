#include "detect/dictionary.hpp"

#include "utf8.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#ifdef LAYOUT_FIX_HAVE_HUNSPELL
#include <memory>
#include <hunspell/hunspell.hxx>
#endif

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

Dictionary::~Dictionary() = default;

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
    if (!load_text(buffer.str())) {
        return false;
    }

#ifdef LAYOUT_FIX_HAVE_HUNSPELL
    auto affix_path = std::filesystem::path(path);
    affix_path.replace_extension(".aff");
    if (std::filesystem::is_regular_file(affix_path)) {
        try {
            hunspell_ = std::make_unique<Hunspell>(affix_path.c_str(), path.c_str());
        } catch (...) {
            hunspell_.reset();
        }
    }
#endif
    return true;
}

bool Dictionary::contains(std::string_view word) const {
    if (word.empty()) {
        return false;
    }
#ifdef LAYOUT_FIX_HAVE_HUNSPELL
    if (hunspell_) {
        return hunspell_->spell(std::string(word)) != 0;
    }
#endif
    return words_.contains(utf8::casefold(word));
}

bool Dictionary::has_morphology() const {
#ifdef LAYOUT_FIX_HAVE_HUNSPELL
    return hunspell_ != nullptr;
#else
    return false;
#endif
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

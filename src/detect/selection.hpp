#pragma once

#include "detect/dictionary.hpp"
#include "lang.hpp"

#include <array>
#include <string>
#include <string_view>
#include <unordered_map>

namespace layoutfix {

struct LayoutMaps {
    std::array<std::array<std::unordered_map<std::string, std::string>, 3>, 3> tables;
};

struct SelectionResult {
    std::string text;
    Lang target = Lang::Other;
    bool changed = false;
};

std::string map_layout_text(std::string_view text, const LayoutMaps& maps,
                            Lang from, Lang to);

SelectionResult convert_selection(
    std::string_view text, const LayoutMaps& maps,
    const std::array<const Dictionary*, 3>& dictionaries,
    const std::array<bool, 3>& active_languages, Lang current_language,
    bool force = true);

bool is_copy_line_editor(std::string_view wm_class, std::string_view wm_name);
bool has_vscode_empty_selection_marker(std::string_view raw_custom_data);
bool looks_like_empty_line_copy(std::string_view selected,
                                std::string_view primary, bool copy_line_editor,
                                std::string_view web_custom_data);

}  // namespace layoutfix


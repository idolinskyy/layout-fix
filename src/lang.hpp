#pragma once

#include <array>
#include <cstddef>

namespace layoutfix {

enum class Lang : std::size_t { En, Uk, Ru, Other };

inline constexpr std::array<Lang, 3> kSupportedLanguages{
    Lang::En, Lang::Uk, Lang::Ru};

constexpr std::size_t lang_index(Lang lang) {
  return static_cast<std::size_t>(lang);
}

}  // namespace layoutfix
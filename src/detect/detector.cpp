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

}  // namespace layoutfix

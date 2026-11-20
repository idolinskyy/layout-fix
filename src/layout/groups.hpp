#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "x11/connection.hpp"

namespace layoutfix {

enum class Lang { En, Uk, Other };

struct KeyHit {
    unsigned keycode = 0;
    bool shift = false;
};

struct Translated {
    enum class Kind { Ignore, Word, Boundary };
    Kind kind = Kind::Ignore;
    std::string text;
};

class Groups {
public:
    explicit Groups(XConnection& x11) : x11_(x11) {}
    ~Groups();
    Groups(const Groups&) = delete;
    Groups& operator=(const Groups&) = delete;

    bool init(bool report = true);
    Lang language_of(int group) const;
    int group_for(Lang lang) const;
    int current_group() const;
    bool wait_until(Lang lang, int timeout_ms) const;
    Translated translate(unsigned keycode, int group, bool shift, bool caps) const;
    std::optional<KeyHit> find_key(int group, std::string_view text) const;

private:
    struct Impl;
    XConnection& x11_;
    Impl* impl_ = nullptr;
};

const char* lang_name(Lang lang);

}  // namespace layoutfix

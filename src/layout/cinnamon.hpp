#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace layoutfix {

struct InputSource {
    std::string id;
    int index = 0;
    bool active = false;
};

class CinnamonLayout {
public:
    CinnamonLayout();
    ~CinnamonLayout();
    CinnamonLayout(const CinnamonLayout&) = delete;
    CinnamonLayout& operator=(const CinnamonLayout&) = delete;

    bool open();
    bool refresh();
    bool consume_sources_changed();
    int index_of(std::string_view id) const;
    bool activate(int index);
    const std::vector<InputSource>& sources() const { return sources_; }

private:
    struct Impl;
    Impl* impl_ = nullptr;
    std::vector<InputSource> sources_;
};

}  // namespace layoutfix

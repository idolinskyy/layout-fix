#pragma once

#include "x11/connection.hpp"

#include <string>
#include <vector>

namespace layoutfix {

class Focus {
public:
    explicit Focus(XConnection& x11) : x11_(x11) {}

    bool install();
    void drain();
    unsigned long window_id() const { return window_; }
    const std::string& wm_class() const { return wm_class_; }
    const std::string& wm_name() const { return wm_name_; }
    bool excluded(const std::vector<std::string>& names) const;

private:
    void refresh();

    XConnection& x11_;
    unsigned long active_atom_ = 0;
    unsigned long window_ = 0;
    std::string wm_class_;
    std::string wm_name_;
};

}  // namespace layoutfix

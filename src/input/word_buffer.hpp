#pragma once

#include <string>
#include <vector>

namespace layoutfix {

struct Stroke {
    unsigned keycode = 0;
    bool shift = false;
    bool word = false;
    std::string current;
    std::string other;
};

class WordBuffer {
public:
    void clear();
    void push_word(unsigned keycode, bool shift, std::string current, std::string other);
    void push_boundary(unsigned keycode, bool shift, std::string text);
    void pop();

    bool empty() const { return strokes_.empty(); }
    int group() const { return group_; }
    void set_group(int group) { group_ = group; }
    const std::string& current_text() const { return current_; }
    const std::string& other_text() const { return other_; }
    const std::vector<Stroke>& strokes() const { return strokes_; }
    bool has_word() const { return !current_.empty(); }

private:
    std::vector<Stroke> strokes_;
    std::string current_;
    std::string other_;
    int group_ = -1;
};

}  // namespace layoutfix

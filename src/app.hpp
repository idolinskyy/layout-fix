#pragma once

#include "config.hpp"

namespace layoutfix {

class App {
public:
    int run(Config config, bool check_only);
};

}  // namespace layoutfix

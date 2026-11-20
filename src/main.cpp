#include "app.hpp"
#include "config.hpp"
#include "log.hpp"

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace {

void print_help() {
    std::cerr << "layout-fix — виправляє слово, набране не в тій розкладці (X11, Cinnamon)\n"
                 "\n"
                 "  layout-fix [--config ШЛЯХ] [--debug-keys] [--check]\n"
                 "\n"
                 "  --check        перевірити X11, розкладки і словники, потім вийти\n"
                 "  --debug-keys   писати набрані символи в stderr. Для звичайної роботи не вмикати\n";
}

class InstanceLock {
public:
    bool acquire() {
        const char* home = std::getenv("HOME");
        if (home == nullptr) {
            return false;
        }
        const auto dir = std::filesystem::path(home) / ".cache";
        std::error_code error;
        std::filesystem::create_directories(dir, error);
        fd_ = ::open((dir / "layout-fix.lock").c_str(), O_RDWR | O_CREAT, 0644);
        if (fd_ < 0) {
            return false;
        }
        if (flock(fd_, LOCK_EX | LOCK_NB) != 0) {
            return false;
        }
        return true;
    }

    ~InstanceLock() {
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }

private:
    int fd_ = -1;
};

}  // namespace

int main(int argc, char** argv) {
    std::string config_path = layoutfix::default_config_path();
    bool config_path_explicit = false;
    bool debug_keys = false;
    bool check_only = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            print_help();
            return 0;
        }
        if (arg == "--debug-keys") {
            debug_keys = true;
            continue;
        }
        if (arg == "--check") {
            check_only = true;
            continue;
        }
        if (arg == "--config" && i + 1 < argc) {
            config_path = argv[++i];
            config_path_explicit = true;
            continue;
        }
        std::cerr << "невідомий аргумент: " << arg << '\n';
        print_help();
        return 2;
    }

    InstanceLock lock;
    if (!check_only && !lock.acquire()) {
        layoutfix::log::info("вже запущено інший примірник");
        return 0;
    }

    auto config = layoutfix::load_config(config_path, debug_keys);
    config.config_path_explicit = config_path_explicit;
    layoutfix::App app;
    return app.run(config, check_only);
}

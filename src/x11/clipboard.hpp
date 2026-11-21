#pragma once

#include "x11/connection.hpp"

#include <string>

union _XEvent;

namespace layoutfix {

class Clipboard {
public:
    explicit Clipboard(XConnection& x11) : x11_(x11) {}
    ~Clipboard();
    Clipboard(const Clipboard&) = delete;
    Clipboard& operator=(const Clipboard&) = delete;

    bool init();
    void handle_event(const _XEvent& event);

    // Читає текст із X11 CLIPBOARD (UTF8_STRING із fallback на XA_STRING).
    bool read(int timeout_ms, std::string& out);
    // Читає текст із X11 PRIMARY (поточне виділення, без зміни CLIPBOARD).
    bool read_primary(int timeout_ms, std::string& out);
    // Читає довільний MIME/target із X11 CLIPBOARD (напр. chromium/x-web-custom-data).
    bool read_custom_target(const char* target_name, int timeout_ms,
                            std::string& out);
    // Тимчасово перехоплює володіння CLIPBOARD, щоб зловити SelectionClear після Ctrl+C.
    bool claim_sentinel();
    // Чекає на втрату володіння CLIPBOARD (програма скопіювала виділення).
    bool wait_for_owner_change(int timeout_ms);
    // Встановлює текст у CLIPBOARD і обслуговує SelectionRequest.
    bool set(std::string text);
    void clear();

    bool is_owning() const { return owning_; }
    bool content_served() const { return content_served_; }

private:
    enum class ReadStatus { Ok, Rejected, Timeout };
    ReadStatus read_target(unsigned long selection_atom,
                           unsigned long target_atom, int timeout_ms,
                           std::string& out);
    void pump_requests();

    XConnection& x11_;
    unsigned long window_ = 0;
    unsigned long clipboard_atom_ = 0;
    unsigned long utf8_atom_ = 0;
    unsigned long targets_atom_ = 0;
    unsigned long text_atom_ = 0;
    unsigned long prop_atom_ = 0;
    unsigned long incr_atom_ = 0;
    bool owning_ = false;
    bool sentinel_ = false;
    bool owner_changed_ = false;
    bool content_served_ = false;
    std::string owned_text_;
};

}  // namespace layoutfix
#include "a11y/password.hpp"

#include "log.hpp"

#include <cstring>

#ifdef LAYOUT_FIX_HAVE_ATSPI
#include <atspi/atspi.h>
#endif

namespace layoutfix {

struct PasswordFocusState {
    bool available = false;
    bool active = false;
    bool announced = false;
#ifdef LAYOUT_FIX_HAVE_ATSPI
    AtspiEventListener* listener = nullptr;
#endif

    void set_active(bool value) {
        if (active == value) {
            return;
        }
        active = value;
        announced = false;
        if (value) {
            log::info("фокус у полі пароля, виправлення призупинено");
        } else {
            log::info("фокус вийшов з поля пароля");
        }
    }
};

#ifdef LAYOUT_FIX_HAVE_ATSPI
namespace {

WidgetRole map_role(AtspiRole role) {
    if (role == ATSPI_ROLE_PASSWORD_TEXT) {
        return WidgetRole::Password;
    }
    if (role == ATSPI_ROLE_ENTRY) {
        return WidgetRole::Entry;
    }
    if (role == ATSPI_ROLE_TEXT) {
        return WidgetRole::Text;
    }
    return WidgetRole::Other;
}

std::string text_input_type(AtspiAccessible* object) {
    if (object == nullptr) {
        return {};
    }
    GError* error = nullptr;
    GHashTable* attributes = atspi_accessible_get_attributes(object, &error);
    if (error != nullptr) {
        g_error_free(error);
        return {};
    }
    if (attributes == nullptr) {
        return {};
    }
    std::string value;
    if (const auto* raw = static_cast<const char*>(
            g_hash_table_lookup(attributes, "text-input-type"))) {
        value = raw;
    }
    g_hash_table_unref(attributes);
    return value;
}

bool accessible_is_password(AtspiAccessible* object) {
    if (object == nullptr) {
        return false;
    }
    GError* error = nullptr;
    const AtspiRole role = atspi_accessible_get_role(object, &error);
    if (error != nullptr) {
        g_error_free(error);
        error = nullptr;
    }
    return is_password_widget(map_role(role), text_input_type(object));
}

void on_focus_event(AtspiEvent* event, void* data) {
    auto* impl = static_cast<PasswordFocusState*>(data);
    if (impl == nullptr || event == nullptr || event->type == nullptr) {
        return;
    }
    const bool state_changed = std::strstr(event->type, "state-changed") != nullptr;
    const bool gained = !state_changed || event->detail1 != 0;
    const bool password = accessible_is_password(event->source);
    if (!gained) {
        if (password) {
            impl->set_active(false);
        }
        return;
    }
    impl->set_active(password);
}

}  // namespace
#endif

PasswordFocus::PasswordFocus() : impl_(std::make_unique<PasswordFocusState>()) {}

PasswordFocus::~PasswordFocus() {
#ifdef LAYOUT_FIX_HAVE_ATSPI
    if (impl_ != nullptr && impl_->listener != nullptr) {
        atspi_event_listener_deregister(impl_->listener, "object:state-changed:focused",
                                        nullptr);
        atspi_event_listener_deregister(impl_->listener, "focus:", nullptr);
        g_object_unref(impl_->listener);
        impl_->listener = nullptr;
    }
#endif
}

bool PasswordFocus::open() {
#ifdef LAYOUT_FIX_HAVE_ATSPI
    atspi_set_timeout(400, 400);
    const int status = atspi_init();
    if (status >= 2) {
        log::info("немає AT-SPI, поля пароля не відстежуються");
        return false;
    }
    impl_->listener = atspi_event_listener_new(on_focus_event, impl_.get(), nullptr);
    if (impl_->listener == nullptr) {
        log::info("немає AT-SPI, поля пароля не відстежуються");
        return false;
    }
    GError* error = nullptr;
    const bool focused_ok = atspi_event_listener_register(
                                impl_->listener, "object:state-changed:focused", &error) != 0;
    if (error != nullptr) {
        g_error_free(error);
        error = nullptr;
    }
    const bool focus_ok =
        atspi_event_listener_register(impl_->listener, "focus:", &error) != 0;
    if (error != nullptr) {
        g_error_free(error);
    }
    if (!focused_ok && !focus_ok) {
        g_object_unref(impl_->listener);
        impl_->listener = nullptr;
        log::info("немає AT-SPI, поля пароля не відстежуються");
        return false;
    }
    impl_->available = true;
    log::info("стежу за полями пароля через AT-SPI");
    return true;
#else
    log::info("зібрано без AT-SPI, поля пароля не відстежуються");
    return false;
#endif
}

bool PasswordFocus::active() const { return impl_->active; }

bool PasswordFocus::take_activated() {
    if (!impl_->active || impl_->announced) {
        return false;
    }
    impl_->announced = true;
    return true;
}

}  // namespace layoutfix

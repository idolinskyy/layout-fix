#include "a11y/password.hpp"

#include "log.hpp"

#include <cstring>
#include <string>

#ifdef LAYOUT_FIX_HAVE_ATSPI
#include <atspi/atspi.h>
#include <gio/gio.h>
#endif

namespace layoutfix {

struct PasswordFocusState {
    bool available = false;
    bool active = false;
    bool announced = false;
    bool closing = false;
#ifdef LAYOUT_FIX_HAVE_ATSPI
    GDBusConnection* bus = nullptr;
    guint subscriptions[2] = {};
    bool registered[2] = {};
    GCancellable* lookup = nullptr;
    unsigned generation = 0;
    unsigned serial = 0;
    std::string password_sender;
    std::string password_path;
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

constexpr const char* kFocusedEvent = "object:state-changed:focused";
constexpr const char* kFocusEvent = "focus:";
constexpr const char* kFocusInterface = "org.a11y.atspi.Event.Focus";

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

struct Lookup {
    PasswordFocusState* state = nullptr;
    unsigned generation = 0;
    unsigned serial = 0;
    std::string sender;
    std::string path;
    bool attributes = false;
};

void drop_lookup(PasswordFocusState& state) {
    if (state.lookup == nullptr) {
        return;
    }
    g_cancellable_cancel(state.lookup);
    g_object_unref(state.lookup);
    state.lookup = nullptr;
}

void apply_password(PasswordFocusState& state, const std::string& sender,
                    const std::string& path, bool password) {
    if (password) {
        state.password_sender = sender;
        state.password_path = path;
    } else {
        state.password_sender.clear();
        state.password_path.clear();
    }
    state.set_active(password);
}

bool send_lookup(PasswordFocusState& state, std::string sender, std::string path,
                 bool attributes);

void on_lookup_reply(GObject* source, GAsyncResult* result, gpointer data) {
    auto* lookup = static_cast<Lookup*>(data);
    PasswordFocusState* state = lookup->state;
    const bool current =
        lookup->generation == state->generation && lookup->serial == state->serial;
    GError* error = nullptr;
    GVariant* reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error);
    if (!current || state->closing) {
        if (error != nullptr) {
            g_error_free(error);
        }
        if (reply != nullptr) {
            g_variant_unref(reply);
        }
        delete lookup;
        return;
    }
    bool password = false;
    bool ask_attributes = false;
    if (error != nullptr || reply == nullptr) {
        if (error != nullptr) {
            g_error_free(error);
        }
    } else if (!lookup->attributes &&
               g_variant_is_of_type(reply, G_VARIANT_TYPE("(u)"))) {
        guint32 role = ATSPI_ROLE_INVALID;
        g_variant_get(reply, "(u)", &role);
        const WidgetRole mapped = map_role(static_cast<AtspiRole>(role));
        if (mapped == WidgetRole::Password) {
            password = true;
        } else if (mapped == WidgetRole::Entry || mapped == WidgetRole::Text) {
            ask_attributes = true;
        }
    } else if (lookup->attributes &&
               g_variant_is_of_type(reply, G_VARIANT_TYPE("(a{ss})"))) {
        GVariant* dict = nullptr;
        g_variant_get(reply, "(@a{ss})", &dict);
        if (dict != nullptr) {
            GVariantIter iter;
            g_variant_iter_init(&iter, dict);
            const char* key = nullptr;
            const char* value = nullptr;
            while (g_variant_iter_next(&iter, "{&s&s}", &key, &value)) {
                if (key != nullptr && value != nullptr &&
                    std::strcmp(key, "text-input-type") == 0 && equals_password(value)) {
                    password = true;
                }
            }
            g_variant_unref(dict);
        }
    }
    if (reply != nullptr) {
        g_variant_unref(reply);
    }
    const std::string sender = lookup->sender;
    const std::string path = lookup->path;
    delete lookup;
    if (ask_attributes) {
        if (send_lookup(*state, sender, path, true)) {
            return;
        }
    }
    apply_password(*state, sender, path, password);
}

bool send_lookup(PasswordFocusState& state, std::string sender, std::string path,
                 bool attributes) {
    if (state.bus == nullptr || sender.empty() || path.empty()) {
        return false;
    }
    ++state.serial;
    drop_lookup(state);
    auto* lookup = new Lookup{&state, state.generation, state.serial, sender, path, attributes};
    state.lookup = g_cancellable_new();
    g_dbus_connection_call(
        state.bus, sender.c_str(), path.c_str(), ATSPI_DBUS_INTERFACE_ACCESSIBLE,
        attributes ? "GetAttributes" : "GetRole", nullptr,
        attributes ? G_VARIANT_TYPE("(a{ss})") : G_VARIANT_TYPE("(u)"),
        G_DBUS_CALL_FLAGS_NONE, 250, state.lookup, on_lookup_reply, lookup);
    return true;
}

void on_focus_signal(GDBusConnection*, const gchar* sender, const gchar* object_path,
                     const gchar*, const gchar* signal_name, GVariant* parameters,
                     gpointer data) {
    auto* state = static_cast<PasswordFocusState*>(data);
    if (state == nullptr || state->closing || sender == nullptr || object_path == nullptr) {
        return;
    }
    const bool state_changed = signal_name != nullptr && std::strcmp(signal_name, "StateChanged") == 0;
    bool gain = true;
    if (state_changed) {
        if (parameters == nullptr || g_variant_n_children(parameters) < 2) {
            return;
        }
        GVariant* detail_value = g_variant_get_child_value(parameters, 0);
        GVariant* detail1_value = g_variant_get_child_value(parameters, 1);
        const bool typed = g_variant_is_of_type(detail_value, G_VARIANT_TYPE_STRING) &&
                           g_variant_is_of_type(detail1_value, G_VARIANT_TYPE_INT32);
        const char* detail = typed ? g_variant_get_string(detail_value, nullptr) : nullptr;
        const gint32 detail1 = typed ? g_variant_get_int32(detail1_value) : 0;
        g_variant_unref(detail_value);
        g_variant_unref(detail1_value);
        if (!typed || detail == nullptr || std::strcmp(detail, "focused") != 0) {
            return;
        }
        gain = detail1 != 0;
    }
    if (!gain) {
        if (state->password_sender == sender && state->password_path == object_path) {
            ++state->generation;
            drop_lookup(*state);
            apply_password(*state, {}, {}, false);
        }
        return;
    }
    if (state->active && state->password_sender == sender && state->password_path == object_path) {
        return;
    }
    ++state->generation;
    if (!send_lookup(*state, sender, object_path, false)) {
        apply_password(*state, {}, {}, false);
    }
}

bool registry_call(GDBusConnection* bus, const char* method, const char* event) {
    GError* error = nullptr;
    GVariant* args = nullptr;
    if (std::strcmp(method, "RegisterEvent") == 0) {
        GVariant* names = g_variant_new_array(G_VARIANT_TYPE_STRING, nullptr, 0);
        args = g_variant_new("(s@ass)", event, names, "");
    } else {
        args = g_variant_new("(s)", event);
    }
    GVariant* reply = g_dbus_connection_call_sync(
        bus, ATSPI_DBUS_NAME_REGISTRY, ATSPI_DBUS_PATH_REGISTRY,
        ATSPI_DBUS_INTERFACE_REGISTRY, method, args, nullptr, G_DBUS_CALL_FLAGS_NONE, 1500,
        nullptr, &error);
    if (reply == nullptr) {
        if (error != nullptr) {
            g_error_free(error);
        }
        return false;
    }
    g_variant_unref(reply);
    return true;
}

void close_bus(PasswordFocusState& state) {
    state.closing = true;
    ++state.generation;
    drop_lookup(state);
    if (state.bus == nullptr) {
        return;
    }
    for (guint id : state.subscriptions) {
        if (id != 0) {
            g_dbus_connection_signal_unsubscribe(state.bus, id);
        }
    }
    state.subscriptions[0] = 0;
    state.subscriptions[1] = 0;
    if (state.registered[0]) {
        registry_call(state.bus, "DeregisterEvent", kFocusedEvent);
    }
    if (state.registered[1]) {
        registry_call(state.bus, "DeregisterEvent", kFocusEvent);
    }
    state.registered[0] = false;
    state.registered[1] = false;
    g_object_unref(state.bus);
    state.bus = nullptr;
}

// Chrome не віддає HTML-поле пароля, доки org.a11y.Status.IsEnabled == false.
// Прапорець після вмикання не вимикаємо: вимкнення під час сесії знімає міст
// доступності в Cinnamon і лишає в його кеші вже звільнені віджети.
void ensure_browser_access() {
    GError* error = nullptr;
    GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
    if (bus == nullptr) {
        if (error != nullptr) {
            g_error_free(error);
        }
        log::info("немає session D-Bus, поля пароля в браузері можуть лишатися невидимими");
        return;
    }
    bool known = false;
    bool enabled = false;
    GVariant* reply = g_dbus_connection_call_sync(
        bus, "org.a11y.Bus", "/org/a11y/bus", "org.freedesktop.DBus.Properties", "Get",
        g_variant_new("(ss)", "org.a11y.Status", "IsEnabled"), G_VARIANT_TYPE("(v)"),
        G_DBUS_CALL_FLAGS_NONE, 1500, nullptr, &error);
    if (reply != nullptr) {
        GVariant* inner = nullptr;
        g_variant_get(reply, "(v)", &inner);
        g_variant_unref(reply);
        if (inner != nullptr && g_variant_is_of_type(inner, G_VARIANT_TYPE_BOOLEAN)) {
            enabled = g_variant_get_boolean(inner) != FALSE;
            known = true;
        }
        if (inner != nullptr) {
            g_variant_unref(inner);
        }
    } else if (error != nullptr) {
        g_error_free(error);
        error = nullptr;
    }
    if (!known || !enabled) {
        GVariant* set_reply = g_dbus_connection_call_sync(
            bus, "org.a11y.Bus", "/org/a11y/bus", "org.freedesktop.DBus.Properties", "Set",
            g_variant_new("(ssv)", "org.a11y.Status", "IsEnabled",
                          g_variant_new_boolean(TRUE)),
            nullptr, G_DBUS_CALL_FLAGS_NONE, 1500, nullptr, &error);
        if (set_reply != nullptr) {
            g_variant_unref(set_reply);
            log::info("увімкнено доступність інтерфейсу для полів пароля в браузері");
        } else {
            if (error != nullptr) {
                g_error_free(error);
            }
            log::info(
                "не вдалося увімкнути доступність інтерфейсу, поля пароля в браузері "
                "можуть лишатися невидимими");
        }
    }
    g_object_unref(bus);
}

}  // namespace
#endif

PasswordFocus::PasswordFocus() : impl_(std::make_unique<PasswordFocusState>()) {}

PasswordFocus::~PasswordFocus() {
#ifdef LAYOUT_FIX_HAVE_ATSPI
    if (impl_ != nullptr) {
        close_bus(*impl_);
    }
#endif
}

bool PasswordFocus::open() {
#ifdef LAYOUT_FIX_HAVE_ATSPI
    // atspi_init() підписується на всі програми і для кожної викликає
    // Cache.GetItems. atk-bridge 2.52 на цьому шляху робить g_object_ref
    // по всьому кешу. Звільнений віджет у кеші Cinnamon валить оболонку.
    // Тут лише реєстрація подій фокуса і роль одного об'єкта.
    GError* error = nullptr;
    GDBusConnection* session = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
    if (session == nullptr) {
        if (error != nullptr) {
            g_error_free(error);
        }
        log::info("немає AT-SPI, поля пароля не відстежуються");
        return false;
    }
    GVariant* address_reply = g_dbus_connection_call_sync(
        session, "org.a11y.Bus", "/org/a11y/bus", "org.a11y.Bus", "GetAddress", nullptr,
        G_VARIANT_TYPE("(s)"), G_DBUS_CALL_FLAGS_NONE, 1500, nullptr, &error);
    g_object_unref(session);
    if (address_reply == nullptr) {
        if (error != nullptr) {
            g_error_free(error);
        }
        log::info("немає AT-SPI, поля пароля не відстежуються");
        return false;
    }
    const char* address = nullptr;
    g_variant_get(address_reply, "(&s)", &address);
    const std::string address_copy = address != nullptr ? address : "";
    g_variant_unref(address_reply);
    if (address_copy.empty()) {
        log::info("немає AT-SPI, поля пароля не відстежуються");
        return false;
    }
    impl_->bus = g_dbus_connection_new_for_address_sync(
        address_copy.c_str(),
        static_cast<GDBusConnectionFlags>(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
                                          G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),
        nullptr, nullptr, &error);
    if (impl_->bus == nullptr) {
        if (error != nullptr) {
            g_error_free(error);
        }
        log::info("немає AT-SPI, поля пароля не відстежуються");
        return false;
    }
    impl_->closing = false;
    impl_->subscriptions[0] = g_dbus_connection_signal_subscribe(
        impl_->bus, nullptr, ATSPI_DBUS_INTERFACE_EVENT_OBJECT, "StateChanged", nullptr,
        "focused", G_DBUS_SIGNAL_FLAGS_NONE, on_focus_signal, impl_.get(), nullptr);
    impl_->subscriptions[1] = g_dbus_connection_signal_subscribe(
        impl_->bus, nullptr, kFocusInterface, nullptr, nullptr, nullptr,
        G_DBUS_SIGNAL_FLAGS_NONE, on_focus_signal, impl_.get(), nullptr);
    impl_->registered[0] = registry_call(impl_->bus, "RegisterEvent", kFocusedEvent);
    impl_->registered[1] = registry_call(impl_->bus, "RegisterEvent", kFocusEvent);
    if (!impl_->registered[0] && !impl_->registered[1]) {
        close_bus(*impl_);
        log::info("немає AT-SPI, поля пароля не відстежуються");
        return false;
    }
    ensure_browser_access();
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

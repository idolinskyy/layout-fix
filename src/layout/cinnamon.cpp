#include "layout/cinnamon.hpp"

#include "log.hpp"

#include <gio/gio.h>

#include <cstring>

namespace layoutfix {

struct CinnamonLayout::Impl {
    GDBusConnection* bus = nullptr;
    guint sources_subscription = 0;
    bool sources_changed = false;

    ~Impl() {
        if (bus != nullptr) {
            if (sources_subscription != 0) {
                g_dbus_connection_signal_unsubscribe(bus, sources_subscription);
            }
            g_object_unref(bus);
        }
    }
};

CinnamonLayout::CinnamonLayout() = default;
CinnamonLayout::~CinnamonLayout() { delete impl_; }

bool CinnamonLayout::open() {
    delete impl_;
    impl_ = new Impl();
    GError* error = nullptr;
    impl_->bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
    if (impl_->bus == nullptr) {
        log::error(error != nullptr ? error->message : "немає session D-Bus");
        if (error != nullptr) {
            g_error_free(error);
        }
        return false;
    }
    impl_->sources_subscription = g_dbus_connection_signal_subscribe(
        impl_->bus, "org.Cinnamon", "org.Cinnamon", "InputSourcesChanged",
        "/org/Cinnamon", nullptr, G_DBUS_SIGNAL_FLAGS_NONE,
        +[](GDBusConnection*, const gchar*, const gchar*, const gchar*,
            const gchar*, GVariant*, gpointer data) {
            static_cast<Impl*>(data)->sources_changed = true;
        },
        impl_, nullptr);
    if (impl_->sources_subscription == 0) {
        log::error("не вдалося підписатися на зміну джерел Cinnamon");
        return false;
    }
    return refresh();
}

bool CinnamonLayout::consume_sources_changed() {
    g_main_context_iteration(nullptr, FALSE);
    if (impl_ == nullptr || !impl_->sources_changed) {
        return false;
    }
    impl_->sources_changed = false;
    return true;
}

bool CinnamonLayout::refresh() {
    sources_.clear();
    if (impl_ == nullptr || impl_->bus == nullptr) {
        return false;
    }
    GError* error = nullptr;
    GVariant* reply = g_dbus_connection_call_sync(
        impl_->bus, "org.Cinnamon", "/org/Cinnamon", "org.Cinnamon", "GetInputSources", nullptr,
        G_VARIANT_TYPE("(a(ssisssssssib))"), G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, &error);
    if (reply == nullptr) {
        log::error(error != nullptr ? error->message : "Cinnamon не віддав список розкладок");
        if (error != nullptr) {
            g_error_free(error);
        }
        return false;
    }

    GVariant* list = g_variant_get_child_value(reply, 0);
    GVariantIter iter;
    g_variant_iter_init(&iter, list);
    const char* type = nullptr;
    const char* id = nullptr;
    int index = 0;
    const char* display = nullptr;
    const char* short_name = nullptr;
    const char* flag = nullptr;
    const char* xkbid = nullptr;
    const char* xkb_layout = nullptr;
    const char* variant = nullptr;
    const char* prefs = nullptr;
    const char* dupe = nullptr;
    gboolean active = FALSE;
    while (g_variant_iter_loop(&iter, "(ssisssssssib)", &type, &id, &index, &display, &short_name,
                               &flag, &xkbid, &xkb_layout, &variant, &prefs, &dupe, &active)) {
        if (std::strcmp(type, "xkb") != 0) {
            continue;
        }
        const char* name = xkb_layout != nullptr && xkb_layout[0] != '\0' ? xkb_layout : id;
        sources_.push_back(InputSource{name, index, active != FALSE});
    }
    g_variant_unref(list);
    g_variant_unref(reply);

    if (index_of("us") < 0 || index_of("ua") < 0) {
        log::error("у Cinnamon мають бути джерела us і ua");
        return false;
    }
    log::info("Cinnamon us=джерело " + std::to_string(index_of("us")) + ", ua=джерело " +
              std::to_string(index_of("ua")));
    return true;
}

int CinnamonLayout::index_of(std::string_view id) const {
    for (const auto& source : sources_) {
        if (source.id == id) {
            return source.index;
        }
    }
    return -1;
}

bool CinnamonLayout::activate(int index) {
    if (impl_ == nullptr || impl_->bus == nullptr) {
        return false;
    }
    GError* error = nullptr;
    GVariant* reply = g_dbus_connection_call_sync(
        impl_->bus, "org.Cinnamon", "/org/Cinnamon", "org.Cinnamon", "ActivateInputSourceIndex",
        g_variant_new("(i)", index), nullptr, G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, &error);
    if (reply == nullptr) {
        log::error(error != nullptr ? error->message : "не вдалося перемкнути розкладку");
        if (error != nullptr) {
            g_error_free(error);
        }
        return false;
    }
    g_variant_unref(reply);
    for (auto& source : sources_) {
        source.active = source.index == index;
    }
    return true;
}

}  // namespace layoutfix

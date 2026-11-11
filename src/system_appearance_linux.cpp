#include "system_appearance.hpp"

#include <gio/gio.h>

#include <memory>

namespace hotbox::detail {
namespace {

constexpr auto service = "org.freedesktop.portal.Desktop";
constexpr auto path = "/org/freedesktop/portal/desktop";
constexpr auto settings = "org.freedesktop.portal.Settings";
constexpr auto appearance_namespace = "org.freedesktop.appearance";
constexpr auto scheme = "color-scheme";

ThemeMode DecodeScheme(GVariant* value) {
    // Read on older portals can return an extra variant wrapper.
    while (g_variant_is_of_type(value, G_VARIANT_TYPE_VARIANT)) {
        auto* child = g_variant_get_variant(value);
        g_variant_unref(value);
        value = child;
    }
    const bool dark = g_variant_is_of_type(value, G_VARIANT_TYPE_UINT32) && g_variant_get_uint32(value) == 1;
    g_variant_unref(value);
    return dark ? ThemeMode::Dark : ThemeMode::Light;
}

class ThreadDefaultContext {
public:
    explicit ThreadDefaultContext(GMainContext* context) : context_(context) {
        g_main_context_push_thread_default(context_);
    }
    ~ThreadDefaultContext() { g_main_context_pop_thread_default(context_); }

private:
    GMainContext* context_;
};

class PortalAppearance {
public:
    PortalAppearance() {
        const ThreadDefaultContext current(context_);
        ++pending_;
        g_dbus_proxy_new_for_bus(G_BUS_TYPE_SESSION, G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES,
            nullptr, service, path, settings, cancel_, ProxyReady, this);
    }

    ~PortalAppearance() {
        g_cancellable_cancel(cancel_);
        if (proxy_)
            g_signal_handlers_disconnect_by_data(proxy_, this);
        while (pending_ > 0)
            g_main_context_iteration(context_, true);
        g_clear_object(&proxy_);
        g_object_unref(cancel_);
        g_main_context_unref(context_);
    }

    ThemeMode read() {
        const ThreadDefaultContext current(context_);
        while (g_main_context_iteration(context_, false)) {}
        return mode_;
    }

private:
    struct Request {
        PortalAppearance& portal;
        unsigned revision;
    };

    static void ProxyReady(GObject*, GAsyncResult* result, gpointer data) {
        auto& portal = *static_cast<PortalAppearance*>(data);
        --portal.pending_;
        portal.proxy_ = g_dbus_proxy_new_for_bus_finish(result, nullptr);
        if (!portal.proxy_ || g_cancellable_is_cancelled(portal.cancel_))
            return;
        g_signal_connect(portal.proxy_, "g-signal", G_CALLBACK(SettingChanged), &portal);
        g_signal_connect(portal.proxy_, "notify::g-name-owner", G_CALLBACK(OwnerChanged), &portal);
        portal.refresh();
    }

    static void OwnerChanged(GObject*, GParamSpec*, gpointer data) {
        static_cast<PortalAppearance*>(data)->refresh();
    }

    static void SettingChanged(GDBusProxy*, const gchar*, const gchar* signal, GVariant* parameters, gpointer data) {
        if (!g_str_equal(signal, "SettingChanged") || !g_variant_is_of_type(parameters, G_VARIANT_TYPE("(ssv)")))
            return;
        const gchar* name = nullptr;
        const gchar* key = nullptr;
        g_variant_get_child(parameters, 0, "&s", &name);
        g_variant_get_child(parameters, 1, "&s", &key);
        if (g_str_equal(name, appearance_namespace) && g_str_equal(key, scheme)) {
            auto& portal = *static_cast<PortalAppearance*>(data);
            ++portal.revision_;
            portal.mode_ = DecodeScheme(g_variant_get_child_value(parameters, 2));
        }
    }

    void refresh() {
        ++revision_;
        auto* owner = g_dbus_proxy_get_name_owner(proxy_);
        if (!owner) {
            mode_ = ThemeMode::Light;
            return;
        }
        g_free(owner);
        ++pending_;
        g_dbus_proxy_call(proxy_, "Read", g_variant_new("(ss)", appearance_namespace, scheme),
            G_DBUS_CALL_FLAGS_NONE, 1000, cancel_, ReadReady, new Request{*this, revision_});
    }

    static void ReadReady(GObject* source, GAsyncResult* result, gpointer data) {
        const std::unique_ptr<Request> request(static_cast<Request*>(data));
        auto& portal = request->portal;
        --portal.pending_;
        auto* reply = g_dbus_proxy_call_finish(G_DBUS_PROXY(source), result, nullptr);
        if (request->revision == portal.revision_) {
            portal.mode_ = reply && g_variant_is_of_type(reply, G_VARIANT_TYPE("(v)"))
                ? DecodeScheme(g_variant_get_child_value(reply, 0)) : ThemeMode::Light;
        }
        if (reply)
            g_variant_unref(reply);
    }

    GMainContext* context_ = g_main_context_new();
    GCancellable* cancel_ = g_cancellable_new();
    GDBusProxy* proxy_ = nullptr;
    unsigned pending_ = 0, revision_ = 0;
    ThemeMode mode_ = ThemeMode::Light;
};

} // namespace

ThemeMode SystemAppearance() {
    static PortalAppearance portal;
    return portal.read();
}

} // namespace hotbox::detail

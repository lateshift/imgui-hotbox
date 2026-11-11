#include "system_appearance.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace hotbox::detail {
namespace {

ThemeMode ReadAppTheme() {
    constexpr auto personalize = L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
    DWORD light = 1;
    DWORD size = static_cast<DWORD>(sizeof(light));
    const auto result = RegGetValueW(HKEY_CURRENT_USER, personalize, L"AppsUseLightTheme",
        RRF_RT_REG_DWORD, nullptr, &light, &size);
    return result == ERROR_SUCCESS && light == 0 ? ThemeMode::Dark : ThemeMode::Light;
}

// A hidden top-level window receives the same settings broadcasts as the host.
class SettingsWindow {
public:
    SettingsWindow() {
        WNDCLASSW type{};
        type.hInstance = instance_;
        type.lpszClassName = name_;
        type.lpfnWndProc = WindowProc;
        registered_ = RegisterClassW(&type) != 0;
        if (registered_) {
            window_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, name_, L"", WS_POPUP,
                0, 0, 0, 0, nullptr, nullptr, instance_, this);
        }
    }

    ~SettingsWindow() {
        if (window_)
            DestroyWindow(window_);
        if (registered_)
            UnregisterClassW(name_, instance_);
    }

    ThemeMode appearance() const { return window_ ? appearance_ : ReadAppTheme(); }

private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        }
        if (message == WM_SETTINGCHANGE || message == WM_THEMECHANGED) {
            auto* self = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
            if (self)
                self->appearance_ = ReadAppTheme();
            return 0;
        }
        return DefWindowProcW(window, message, wparam, lparam);
    }

    static constexpr auto name_ = L"ImGuiHotboxSystemAppearance";
    HINSTANCE instance_ = GetModuleHandleW(nullptr);
    HWND window_ = nullptr;
    bool registered_ = false;
    ThemeMode appearance_ = ReadAppTheme();
};

} // namespace

ThemeMode SystemAppearance() {
    static SettingsWindow settings;
    return settings.appearance();
}

} // namespace hotbox::detail

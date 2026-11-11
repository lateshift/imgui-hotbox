#pragma once

#include <hotbox/fonts.hpp>
#include <hotbox/log.hpp>
#include <hotbox/theme.hpp>

#include <hotbox/config.hpp>
#include <hotbox/registry.hpp>

#include <span>
#include <string>
#include <string_view>

namespace hotbox {

struct MotionPrefs {
    float speed = 1.0f; // 0.1 for slow motion, 0 for immediate transitions.
    bool reduced_motion = false;
};

/// Call once after NewFrame(). Pass true if the host already advanced ImAnim.
void begin_frame(bool imanim_already_updated = false);

/// Immediate-mode widget. Its registry must outlive it.
class Hotbox {
public:
    explicit Hotbox(Registry& registry, Config config = {});
    ~Hotbox();
    Hotbox(const Hotbox&) = delete;
    Hotbox& operator=(const Hotbox&) = delete;

    /// Submit once between NewFrame() and Render(), after the host UI.
    void frame();

    void open(ImVec2 screen_pos);
    void close(bool animated = true);
    bool is_open() const;
    void set_pinned(bool pinned);
    void set_motion(MotionPrefs prefs);
    MotionPrefs motion_prefs() const;
    /// True when another frame is needed for a visual transition or confirmation.
    bool is_animating() const;

    void set_theme(const Theme& light, const Theme& dark);
    void set_theme_mode(ThemeMode mode);
    /// Override the OS appearance in System mode. Return System or pass an empty provider to use the OS.
    void set_system_appearance_provider(std::function<ThemeMode()> provider);
    /// The resolved theme target. System appearance is refreshed during frame().
    const Theme& current_theme() const;

    /// Recently invoked commands, most recent first, at most eight.
    std::span<const CommandId> recent_commands() const;
    /// Forget recent commands and the use counts that rank search results.
    void clear_history();
    /// The state saved in imgui.ini (recent commands, use times, menu set, row options) as text,
    /// for hosts that keep their settings elsewhere.
    std::string export_state() const;
    /// Replace the state with text from export_state(). Unknown lines are ignored.
    void import_state(std::string_view state);

    std::function<void()> on_open, on_close, on_trigger_tap;
    std::function<void(const InvokeContext&)> on_invoked;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace hotbox

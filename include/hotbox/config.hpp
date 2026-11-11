#pragma once

#include <imgui.h>
#include <functional>
#include <string>

namespace hotbox {

struct Config {
    ImGuiKeyChord trigger = ImGuiMod_Alt | ImGuiKey_Space;
    float hold_threshold_ms = 180;
    float reveal_delay_ms = 120;
    float markahead_window_ms = 350;
    float submenu_dwell_ms = 280;
    float dead_zone_pt = 12;
    bool show_menu_rows = true;
    bool show_zone_ticks = true;
    /// Typing while the hotbox is open searches every registered command.
    bool enable_search = true;
    bool monogram_fallback = true;
    float scrim_opacity = 0.35f;
    /// How long the pointer or keyboard focus rests on a command before its tooltip appears.
    float tooltip_delay_ms = 500;
    /// Section name for recent commands, the menu set and row options in imgui.ini, saved as
    /// `[Hotbox][<settings_id>]`. Give each hotbox in a context its own ID. Empty disables it.
    std::string settings_id = "Default";
    std::function<bool()> can_open;
};

} // namespace hotbox

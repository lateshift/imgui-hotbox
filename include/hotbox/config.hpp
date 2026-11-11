#pragma once

#include <imgui.h>
#include <functional>

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
    bool enable_search = true;
    bool monogram_fallback = true;
    float scrim_opacity = 0.35f;
    std::function<bool()> can_open;
};

} // namespace hotbox

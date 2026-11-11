#pragma once

#include <imgui.h>
#include <array>
#include <string_view>

namespace hotbox {

enum class ThemeMode { Light, Dark, System };
enum class ShapeSetting { Rounded, Square };
enum class Material { Solid, Frost };

struct TextStyle {
    std::string_view face;
    float size_pt;
    ImVec4 ink;
};

/// An immutable view of one mode of the generated IFCStudio tokens.
struct DesignTokens {
    bool dark = true;

    /// Look up a token by its design-system name without the mode suffix, e.g. "surface/app".
    ImVec4 color(std::string_view name) const;
    TextStyle text(std::string_view name) const;
};

struct HotboxColors {
    ImVec4 scrim, surface, edge, shadow;
    ImVec4 pill, pill_hover, pill_pressed, pill_ink, pill_shadow;
    ImVec4 sector, sector_edge, trail, ring;
    ImVec4 ink, ink_secondary, disabled, accent, accent_ink, focus;
    ImVec4 key_fill, key_edge, key_ink;
};

struct MotionTokens {
    float hover = 0.100f;
    float popup = 0.140f;
    float section = 0.180f;
    float move = 0.100f;
    float stagger = 0.010f;
};

struct Theme {
    DesignTokens tokens;
    HotboxColors colors;
    ShapeSetting shape = ShapeSetting::Rounded;
    Material material = Material::Solid;
    MotionTokens motion;

    static Theme IFCStudioDark();
    static Theme IFCStudioLight();
};

struct ContrastCheck {
    std::string_view label;
    float ratio;
    float minimum;
};

/// Check the lowest contrast across the Solid surface and each role's painted states.
std::array<ContrastCheck, 6> CheckSolidContrast(const Theme& theme);

/// Map IFCStudio tokens to the host's Dear ImGui style.
void ApplyImGuiTheme(const Theme& theme, ImGuiStyle& style);

} // namespace hotbox

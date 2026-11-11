#include <hotbox/theme.hpp>
#include <hotbox/fonts.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

namespace hotbox {
namespace {

constexpr ImVec4 Decode(std::uint32_t rgba) {
    constexpr float normalize = 1.0f / 255.0f;
    return {static_cast<float>(rgba >> 24) * normalize,
            static_cast<float>((rgba >> 16) & 0xffu) * normalize,
            static_cast<float>((rgba >> 8) & 0xffu) * normalize,
            static_cast<float>(rgba & 0xffu) * normalize};
}

struct ColorPair {
    std::string_view name;
    ImVec4 dark, light;
};

constexpr ColorPair palette[] = {
#define COLOR(name, dark, light) {name, Decode(dark), Decode(light)},
#define TYPE(name, dark, face, size, ink)
#include "design_tokens.inc"
#undef TYPE
#undef COLOR
};

struct TypeToken {
    std::string_view name;
    bool dark;
    TextStyle text;
};

constexpr TypeToken typography[] = {
#define COLOR(name, dark, light)
#define TYPE(name, dark, face, size, ink) {name, dark, {face, static_cast<float>(size), Decode(ink)}},
#include "design_tokens.inc"
#undef TYPE
#undef COLOR
};

Theme IFCStudio(bool dark) {
    Theme theme{};
    theme.tokens.dark = dark;
    const auto c = [&](std::string_view name) { return theme.tokens.color(name); };
    theme.colors = {
        c("hotbox/scrim"), c("hotbox/surface"), c("hotbox/edge"), c("hotbox/shadow"),
        c("hotbox/pill"), c("hotbox/pill-hover"), c("hotbox/pill-pressed"), c("hotbox/pill-ink"),
        c("hotbox/pill-shadow"), c("hotbox/sector"), c("hotbox/sector-edge"), c("hotbox/trail"),
        c("hotbox/ring"), c("hotbox/ink"), c("hotbox/ink-secondary"), c("hotbox/disabled"),
        c("hotbox/accent"), c("hotbox/accent-ink"), c("hotbox/focus"),
        c("hotbox/key-fill"), c("hotbox/key-edge"), c("hotbox/key-ink"),
    };
    return theme;
}

ImVec4 Composite(ImVec4 ink, ImVec4 surface) {
    return {std::lerp(surface.x, ink.x, ink.w), std::lerp(surface.y, ink.y, ink.w),
        std::lerp(surface.z, ink.z, ink.w), 1};
}

float Luminance(ImVec4 color) {
    const auto linear = [](float v) {
        return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
    };
    return 0.2126f * linear(color.x) + 0.7152f * linear(color.y) + 0.0722f * linear(color.z);
}

float Contrast(ImVec4 ink, ImVec4 surface) {
    const float a = Luminance(Composite(ink, surface)) + 0.05f;
    const float b = Luminance(surface) + 0.05f;
    return std::max(a, b) / std::min(a, b);
}

} // namespace

ImVec4 DesignTokens::color(std::string_view name) const {
    for (const auto& pair : palette) {
        if (pair.name == name)
            return dark ? pair.dark : pair.light;
    }
    throw std::invalid_argument("Unknown IFCStudio color: " + std::string(name));
}

TextStyle DesignTokens::text(std::string_view name) const {
    for (const auto& token : typography) {
        if (token.name == name && token.dark == dark)
            return token.text;
    }
    throw std::invalid_argument("Unknown IFCStudio text style: " + std::string(name));
}

Theme Theme::IFCStudioDark() { return IFCStudio(true); }
Theme Theme::IFCStudioLight() { return IFCStudio(false); }

std::array<ContrastCheck, 6> CheckSolidContrast(const Theme& theme) {
    const auto& c = theme.colors;
    float body = std::numeric_limits<float>::infinity();
    float secondary = body, disabled = body, focus = body, icons = body, controls = body;
    for (const float backdrop : {0.0f, 1.0f}) {
        const auto rest = Composite(c.surface, {backdrop, backdrop, backdrop, 1});
        disabled = std::min(disabled, Contrast(c.disabled, rest));
        for (const auto surface : {rest, Composite(c.pill_hover, rest),
                Composite(c.pill_pressed, rest), Composite(c.sector, rest)}) {
            body = std::min({body, Contrast(c.ink, surface), Contrast(c.pill_ink, surface)});
            secondary = std::min(secondary, Contrast(c.ink_secondary, surface));
            focus = std::min(focus, Contrast(c.focus, surface));
            icons = std::min({icons, Contrast(c.ink, surface), Contrast(c.accent, surface)});
            controls = std::min(controls, Contrast(c.ring, surface));
        }
    }
    return {{{"Body text", body, 4.5f}, {"Secondary text", secondary, 3.0f},
        {"Disabled text", disabled, 3.8f}, {"Focus ring", focus, 3.0f},
        {"Icon ink", icons, 3.0f}, {"Controls", controls, 3.0f}}};
}

void ApplyImGuiTheme(const Theme& theme, ImGuiStyle& style) {
    const bool rounded = theme.shape == ShapeSetting::Rounded;
    const float target = rounded ? 4.0f : 0.0f;
    const float floating = rounded ? 6.0f : 0.0f;
    const ImFont* font = nullptr;
    if (ImGui::GetCurrentContext()) {
        const auto& io = ImGui::GetIO();
        font = io.FontDefault;
        if (!font && !io.Fonts->Fonts.empty())
            font = io.Fonts->Fonts.front();
    }
    style.FontSizeBase = fonts::imgui_size(font, theme.tokens.text("type/value").size_pt);
    style.WindowPadding = {12, 12};
    style.FramePadding = {8, 6};
    style.ItemSpacing = {8, 4};
    style.WindowRounding = floating;
    style.ChildRounding = target;
    style.FrameRounding = target;
    style.PopupRounding = floating;
    style.ScrollbarRounding = target;
    style.GrabRounding = target;
    style.TabRounding = target;
    style.TabBarOverlineSize = 2;
#ifdef IMGUI_HAS_DOCK
    style.DockingSeparatorSize = 4;
#endif
    style.AntiAliasedLines = true;
    style.AntiAliasedLinesUseTex = true;
    style.AntiAliasedFill = true;
    style.CircleTessellationMaxError = 0.10f;

    const auto c = [&](std::string_view name) { return theme.tokens.color(name); };
    const auto transparent = ImVec4{};
    auto& colors = style.Colors;
    colors[ImGuiCol_Text] = theme.colors.ink;
    colors[ImGuiCol_TextDisabled] = theme.colors.disabled;
    colors[ImGuiCol_WindowBg] = c("surface/panel");
    colors[ImGuiCol_ChildBg] = c("surface/app");
    colors[ImGuiCol_PopupBg] = c("surface/popup");
    colors[ImGuiCol_Border] = theme.colors.edge;
    colors[ImGuiCol_BorderShadow] = transparent;
    colors[ImGuiCol_FrameBg] = c("surface/input");
    colors[ImGuiCol_FrameBgHovered] = theme.colors.pill_hover;
    colors[ImGuiCol_FrameBgActive] = theme.colors.pill_pressed;
    colors[ImGuiCol_TitleBg] = c("surface/app");
    colors[ImGuiCol_TitleBgActive] = theme.colors.surface;
    colors[ImGuiCol_TitleBgCollapsed] = c("surface/panel");
    colors[ImGuiCol_MenuBarBg] = theme.colors.surface;
    colors[ImGuiCol_ScrollbarBg] = transparent;
    colors[ImGuiCol_ScrollbarGrab] = theme.colors.edge;
    colors[ImGuiCol_ScrollbarGrabHovered] = theme.colors.ring;
    colors[ImGuiCol_ScrollbarGrabActive] = theme.colors.ink_secondary;
    colors[ImGuiCol_CheckMark] = theme.colors.accent;
    colors[ImGuiCol_CheckboxSelectedBg] = theme.colors.sector;
    colors[ImGuiCol_SliderGrab] = theme.colors.ring;
    colors[ImGuiCol_SliderGrabActive] = theme.colors.accent;
    colors[ImGuiCol_Button] = theme.colors.key_fill;
    colors[ImGuiCol_ButtonHovered] = theme.colors.pill_hover;
    colors[ImGuiCol_ButtonActive] = theme.colors.pill_pressed;
    colors[ImGuiCol_Header] = theme.colors.pill;
    colors[ImGuiCol_HeaderHovered] = theme.colors.pill_hover;
    colors[ImGuiCol_HeaderActive] = theme.colors.sector;
    colors[ImGuiCol_Separator] = theme.colors.edge;
    colors[ImGuiCol_SeparatorHovered] = theme.colors.ring;
    colors[ImGuiCol_SeparatorActive] = theme.colors.focus;
    colors[ImGuiCol_ResizeGrip] = theme.colors.edge;
    colors[ImGuiCol_ResizeGripHovered] = theme.colors.ring;
    colors[ImGuiCol_ResizeGripActive] = theme.colors.focus;
    colors[ImGuiCol_InputTextCursor] = theme.colors.accent;
    colors[ImGuiCol_TabHovered] = theme.colors.pill_hover;
    colors[ImGuiCol_Tab] = theme.colors.pill;
    colors[ImGuiCol_TabSelected] = theme.colors.sector;
    colors[ImGuiCol_TabSelectedOverline] = theme.colors.accent;
    colors[ImGuiCol_TabDimmed] = c("surface/panel");
    colors[ImGuiCol_TabDimmedSelected] = c("selection/inactive");
    colors[ImGuiCol_TabDimmedSelectedOverline] = theme.colors.ring;
#ifdef IMGUI_HAS_DOCK
    colors[ImGuiCol_DockingPreview] = theme.colors.sector;
    colors[ImGuiCol_DockingEmptyBg] = c("surface/app");
#endif
    colors[ImGuiCol_PlotLines] = theme.colors.ink_secondary;
    colors[ImGuiCol_PlotLinesHovered] = theme.colors.ink;
    colors[ImGuiCol_PlotHistogram] = theme.colors.accent;
    colors[ImGuiCol_PlotHistogramHovered] = c("accent/hover");
    colors[ImGuiCol_TableHeaderBg] = theme.colors.pill;
    colors[ImGuiCol_TableBorderStrong] = theme.colors.ring;
    colors[ImGuiCol_TableBorderLight] = theme.colors.edge;
    colors[ImGuiCol_TableRowBg] = transparent;
    colors[ImGuiCol_TableRowBgAlt] = c("surface/app");
    colors[ImGuiCol_TextLink] = theme.colors.accent;
    colors[ImGuiCol_TextSelectedBg] = theme.colors.sector;
    colors[ImGuiCol_TreeLines] = theme.colors.edge;
    colors[ImGuiCol_DragDropTarget] = theme.colors.focus;
    colors[ImGuiCol_DragDropTargetBg] = theme.colors.sector;
    colors[ImGuiCol_UnsavedMarker] = theme.colors.ink_secondary;
    colors[ImGuiCol_NavCursor] = theme.colors.focus;
    colors[ImGuiCol_NavWindowingHighlight] = theme.colors.focus;
    colors[ImGuiCol_NavWindowingDimBg] = theme.colors.scrim;
    colors[ImGuiCol_ModalWindowDimBg] = theme.colors.scrim;
}

} // namespace hotbox

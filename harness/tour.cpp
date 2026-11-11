#include "tour.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>

namespace harness {
namespace {

// The tour opens the hotbox at the window centre. Positions are logical points in
// the 960 × 600 offscreen viewport and match the demo layout in demo_commands.cpp.
constexpr ImVec2 rest{640, 470};
constexpr ImVec2 origin{480, 300};
constexpr ImVec2 edit_title{277, 214};
constexpr ImVec2 duplicate_row{326, 305};
constexpr ImVec2 north_zone{480, 140};
constexpr ImVec2 north_east{548, 78};
constexpr ImVec2 rigging_row{470, 363};

constexpr std::string_view open_caption = "Hold ⌥ Space: the hotbox opens at the pointer";
constexpr std::string_view menu_caption = "Click a menu, then release Space over a command to run it";
constexpr std::string_view mark_caption = "Press in a zone, drag toward a command, release";
constexpr std::string_view flick_caption = "Once you know it, just flick: the menu never appears";
constexpr std::string_view sets_caption = "The centre button switches menu sets";
constexpr std::string_view search_caption = "Or just type to search every command, then press Enter";

/// A pointer target and the button and key state from `time` on. The pointer
/// eases from the previous key's position, arriving at `time`.
struct Key {
    float time;
    ImVec2 pointer;
    bool left, alt, space;
    std::string_view caption;
    std::string_view typed = {};
    bool enter = false;
};

constexpr std::array keys{
    Key{0.0f, rest, false, false, false, {}},
    Key{0.4f, rest, false, false, false, open_caption},
    Key{1.1f, origin, false, false, false, open_caption},
    Key{1.3f, origin, false, true, false, open_caption},
    Key{1.35f, origin, false, true, true, open_caption},
    Key{2.3f, origin, false, true, true, menu_caption},
    Key{2.9f, edit_title, false, true, true, menu_caption},
    Key{3.1f, edit_title, true, true, true, menu_caption},
    Key{3.17f, edit_title, false, true, true, menu_caption},
    Key{3.5f, edit_title, false, true, true, menu_caption},
    Key{4.0f, duplicate_row, false, true, true, menu_caption},
    Key{4.3f, duplicate_row, false, true, false, menu_caption},
    Key{4.35f, duplicate_row, false, false, false, menu_caption},
    Key{5.4f, duplicate_row, false, false, false, mark_caption},
    Key{5.9f, origin, false, false, false, mark_caption},
    Key{6.1f, origin, false, true, false, mark_caption},
    Key{6.15f, origin, false, true, true, mark_caption},
    Key{6.6f, origin, false, true, true, mark_caption},
    Key{7.1f, north_zone, false, true, true, mark_caption},
    Key{7.3f, north_zone, true, true, true, mark_caption},
    Key{7.7f, north_zone, true, true, true, mark_caption},
    Key{8.3f, north_east, true, true, true, mark_caption},
    Key{8.8f, north_east, false, true, true, mark_caption},
    Key{9.1f, north_east, false, false, false, mark_caption},
    Key{9.6f, north_east, false, false, false, flick_caption},
    Key{10.1f, origin, false, false, false, flick_caption},
    Key{10.3f, origin, false, true, false, flick_caption},
    Key{10.35f, origin, false, true, true, flick_caption},
    Key{10.8f, origin, false, true, true, flick_caption},
    Key{11.2f, north_zone, false, true, true, flick_caption},
    Key{11.5f, north_zone, true, true, true, flick_caption},
    Key{11.6f, north_east, true, true, true, flick_caption},
    Key{11.7f, north_east, false, true, true, flick_caption},
    Key{12.1f, north_east, false, false, false, flick_caption},
    Key{13.0f, north_east, false, false, false, sets_caption},
    Key{13.5f, origin, false, false, false, sets_caption},
    Key{13.7f, origin, false, true, false, sets_caption},
    Key{13.75f, origin, false, true, true, sets_caption},
    Key{14.3f, origin, true, true, true, sets_caption},
    Key{14.37f, origin, false, true, true, sets_caption},
    Key{14.7f, origin, false, true, true, sets_caption},
    Key{15.2f, rigging_row, false, true, true, sets_caption},
    Key{15.5f, rigging_row, true, true, true, sets_caption},
    Key{15.57f, rigging_row, false, true, true, sets_caption},
    Key{16.6f, rigging_row, false, true, false, sets_caption},
    Key{16.65f, rigging_row, false, false, false, sets_caption},
    Key{17.3f, rigging_row, false, false, false, search_caption},
    Key{17.8f, origin, false, false, false, search_caption},
    Key{18.0f, origin, false, true, false, search_caption},
    Key{18.05f, origin, false, true, true, search_caption},
    Key{18.6f, origin, false, true, true, search_caption, "e"},
    Key{18.75f, origin, false, true, true, search_caption, "ex"},
    Key{18.9f, origin, false, true, true, search_caption, "ext"},
    Key{19.4f, origin, false, false, false, search_caption, "ext"},
    Key{20.1f, origin, false, false, false, search_caption, "ext", true},
    Key{20.17f, origin, false, false, false, search_caption, "ext"},
    Key{20.9f, origin, false, false, false, {}},
    Key{21.6f, rest, false, false, false, {}},
    Key{22.0f, rest, false, false, false, {}},
};

float Smooth(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}

ImU32 Color(ImVec4 color, float alpha = 1) {
    color.w *= alpha;
    return ImGui::ColorConvertFloat4ToU32(color);
}

/// Draw a key cap and return its right edge.
float KeyCap(ImDrawList& draw, const hotbox::Theme& theme, ImFont* font, float size, ImVec2 pos,
    std::string_view label, bool down) {
    const auto text = font->CalcTextSizeA(size, FLT_MAX, 0, label.data(), label.data() + label.size());
    const ImVec2 max{pos.x + std::max(28.0f, text.x + 20), pos.y + 28};
    const auto& colors = theme.colors;
    draw.AddRectFilled(pos, max, Color(down ? colors.accent : colors.key_fill), 6);
    draw.AddRect(pos, max, Color(down ? colors.accent : colors.key_edge), 6);
    draw.AddText(font, size, {std::round((pos.x + max.x - text.x) / 2), std::round(pos.y + (28 - text.y) / 2)},
        Color(down ? colors.accent_ink : colors.key_ink), label.data(), label.data() + label.size());
    return max.x;
}

void Pointer(ImDrawList& draw, const hotbox::Theme& theme, ImVec2 tip, bool pressed) {
    if (pressed)
        draw.AddCircleFilled(tip, 13, Color(theme.colors.accent, 0.35f), 32);
    constexpr std::array<ImVec2, 7> arrow{{{0, 0}, {0, 17}, {4.5f, 13}, {7.5f, 19.5f}, {10, 18.5f}, {7, 12}, {12, 12}}};
    std::array<ImVec2, arrow.size()> points{};
    for (std::size_t i = 0; i < arrow.size(); ++i)
        points[i] = {tip.x + arrow[i].x, tip.y + arrow[i].y};
    draw.AddConcavePolyFilled(points.data(), static_cast<int>(points.size()), IM_COL32(255, 255, 255, 255));
    draw.AddPolyline(points.data(), static_cast<int>(points.size()), IM_COL32(0, 0, 0, 255), ImDrawFlags_Closed, 1.0f);
}

} // namespace

int TourFrameCount() { return static_cast<int>(std::ceil(keys.back().time * tour_fps)); }

TourInput TourAt(float seconds) {
    const auto next = std::ranges::upper_bound(keys, seconds, {}, &Key::time);
    const auto& current = *std::prev(next);
    TourInput input{current.pointer, current.left, current.alt, current.space, current.caption};
    input.typed = current.typed;
    input.enter = current.enter;
    input.show_enter = current.caption == search_caption;
    if (next != keys.end()) {
        const float t = Smooth((seconds - current.time) / (next->time - current.time));
        input.pointer = {current.pointer.x + (next->pointer.x - current.pointer.x) * t,
            current.pointer.y + (next->pointer.y - current.pointer.y) * t};
    }
    auto first = std::prev(next);
    while (first != keys.begin() && std::prev(first)->caption == current.caption)
        --first;
    input.caption_age = seconds - first->time;
    return input;
}

void FeedTour(const TourInput& previous, const TourInput& current, ImGuiIO& io) {
    io.AddMousePosEvent(current.pointer.x, current.pointer.y);
    if (current.alt != previous.alt) {
        io.AddKeyEvent(ImGuiMod_Alt, current.alt);
        io.AddKeyEvent(ImGuiKey_LeftAlt, current.alt);
    }
    if (current.space != previous.space)
        io.AddKeyEvent(ImGuiKey_Space, current.space);
    if (current.left != previous.left)
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, current.left);
    if (current.typed.size() > previous.typed.size() && current.typed.starts_with(previous.typed)) {
        for (const char ch : current.typed.substr(previous.typed.size()))
            io.AddInputCharacter(static_cast<unsigned char>(ch));
    }
    if (current.enter != previous.enter)
        io.AddKeyEvent(ImGuiKey_Enter, current.enter);
}

void DrawTourOverlay(ImDrawList& draw, const hotbox::Theme& theme, const hotbox::fonts::Faces& faces,
    const TourInput& input, ImVec2 display) {
    ImFont* font = faces.medium ? faces.medium : ImGui::GetFont();
    const float size = hotbox::fonts::imgui_size(font, 13);
    const float bottom = display.y - 24;

    float x = KeyCap(draw, theme, font, size, {24, bottom - 28}, "⌥", input.alt);
    x = KeyCap(draw, theme, font, size, {x + 6, bottom - 28}, "Space", input.space);
    if (input.show_enter)
        KeyCap(draw, theme, font, size, {x + 6, bottom - 28}, "Enter", input.enter);

    if (!input.caption.empty()) {
        const float alpha = std::clamp(input.caption_age / 0.2f, 0.0f, 1.0f);
        const auto& text = input.caption;
        const auto extent = font->CalcTextSizeA(size, FLT_MAX, 0, text.data(), text.data() + text.size());
        const ImVec2 min{std::round((display.x - extent.x) / 2 - 14), bottom - 32};
        const ImVec2 max{std::round((display.x + extent.x) / 2 + 14), bottom};
        draw.AddRectFilled(min, max, Color(theme.colors.surface, alpha), 8);
        draw.AddRect(min, max, Color(theme.colors.edge, alpha), 8);
        draw.AddText(font, size, {min.x + 14, std::round(min.y + (32 - extent.y) / 2)},
            Color(theme.colors.ink, alpha), text.data(), text.data() + text.size());
    }
    Pointer(draw, theme, input.pointer, input.left);
}

} // namespace harness

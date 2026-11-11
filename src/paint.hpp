#pragma once

#include "atlas.hpp"
#include "geometry.hpp"

#include <span>

namespace hotbox::detail {

enum class TextRole { Value, Caption, Section };

struct PixelGrid {
    Point origin;
    Point density;

    Point physical(Point value) const;
    Point logical(Point value) const;
    Point snap(Point value) const;
};

class Typography {
public:
    Typography() = default;
    Typography(const Theme& theme, float scale, float density);
    float width(std::string_view text, TextRole role = TextRole::Value) const;
    void label(ImDrawList& draw, PixelGrid grid, Rect bounds, std::string_view text,
        float inset, ImU32 color, TextRole role = TextRole::Value) const;
    bool glyph(ImDrawList& draw, PixelGrid grid, Rect bounds, ImWchar codepoint,
        ImFont* font, ImU32 color) const;
    void warm(const RegistrySnapshot& snapshot) const;

private:
    struct Face {
        ImFont* font = nullptr;
        float size = 0;
    };
    std::array<Face, 3> faces_{};
    ImFont* lucide_ = nullptr;
    float density_ = 1;
    float icon_size_ = 16;
};

class Painter {
public:
    Painter(ImDrawList& draw, const Theme& theme, const AtlasAssets& atlas, PixelGrid grid, float scale);

    void opacity(float value) { opacity_ = value; }
    void rounding(float value) { rounding_ = value; }
    ImU32 ink(ImVec4 value) const;
    ImU32 tint(ImU32 value) const;
    void scrim(Rect rect, float opacity);
    void shell(Rect rect);
    void menu(Rect rect);
    void target(Rect rect, ImVec4 fill, float outline);
    void focus(Rect rect);
    void key_cap(Rect rect);
    void caret(Point from, Point to);
    void separator(Point from, Point to);
    void option_box(Rect rect, ImU32 color);
    void check_mark(Rect rect, bool enabled);
    void zone_tick(Point from, Point to);
    void rubber_band(Point from, Point to);
    void press_point(Point pivot);
    void ink_mark(std::span<const Point> ink);
    void icon(const Typography& type, Rect slot, const Icon& icon, bool enabled);
    void monogram(const Typography& type, Rect slot, std::string_view label, bool enabled);

    static ImU32 color(ImVec4 value);

private:
    template<class Draw>
    void device_geometry(const Draw& draw);
    void rounded_rect(Rect rect, float radius, ImU32 fill, ImU32 stroke = 0, float border_px = 1);
    void line(Point from, Point to, ImU32 color, float pixels = 1);
    void shadow(Rect rect, const ShadowSlice& slice, ImU32 color, float offset);
    float radius(float pt) const;

    ImDrawList& draw_;
    const Theme& theme_;
    const AtlasAssets& atlas_;
    PixelGrid grid_;
    float scale_;
    float opacity_ = 1;
    float rounding_ = 1;
};

} // namespace hotbox::detail

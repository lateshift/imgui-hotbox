#include "paint.hpp"

#include <hotbox/fonts.hpp>
#include <imgui_internal.h>

#ifdef HOTBOX_WITH_FREETYPE
#include <imgui_freetype.h>
#endif

#include <algorithm>
#include <cmath>

namespace hotbox::detail {
namespace {

ImVec2 Vec(Point p) { return {p.x, p.y}; }
Point PointOf(ImVec2 p) { return {p.x, p.y}; }

Point Round(Point point) { return {std::round(point.x), std::round(point.y)}; }
Point PixelCenter(Point point) { return {std::floor(point.x) + 0.5f, std::floor(point.y) + 0.5f}; }

class DensityScope {
public:
    DensityScope(ImFont& font, float density) : font_(font), original_(font.CurrentRasterizerDensity) {
        font_.CurrentRasterizerDensity = density;
    }
    ~DensityScope() { font_.CurrentRasterizerDensity = original_; }

private:
    ImFont& font_;
    float original_;
};

ImFont* FindFont(std::string_view name) {
    for (auto* font : ImGui::GetIO().Fonts->Fonts) {
        if (font->GetDebugName() == name)
            return font;
    }
    return nullptr;
}

float PaintSize(const ImFont* font, float em, float density) {
    const float size = fonts::imgui_size(font, em);
#ifdef HOTBOX_WITH_FREETYPE
    // Avoid shrinking a grid-fitted bitmap when ImGui rounds its bake size up.
    if (density < 2 && !font->Sources.empty() &&
        (font->Sources[0]->FontLoaderFlags & ImGuiFreeTypeLoaderFlags_HintByDensity))
        return std::max(size, ImGui::GetRoundedFontSize(size));
#else
    (void)density;
#endif
    return size;
}

bool IsTurn(Point before, Point at, Point after) {
    const auto a = at - before;
    const auto b = after - at;
    return Dot(a, b) < 0.64278761f * Length(a) * Length(b);
}

Point CatmullRom(Point a, Point b, Point c, Point d, float t) {
    const float t2 = t * t;
    return (b * 2 + (c - a) * t + (a * 2 - b * 5 + c * 4 - d) * t2 +
        (b * 3 - c * 3 + d - a) * (t2 * t)) * 0.5f;
}

} // namespace

Point PixelGrid::physical(Point value) const {
    return {(value.x - origin.x) * density.x, (value.y - origin.y) * density.y};
}
Point PixelGrid::logical(Point value) const {
    return {value.x / density.x + origin.x, value.y / density.y + origin.y};
}
Point PixelGrid::snap(Point value) const { return logical(Round(physical(value))); }

Typography::Typography(const Theme& theme, float scale, float density)
    : lucide_(FindFont("Lucide")), density_(density), icon_size_(16 * scale) {
    auto* regular = FindFont("Inter Regular");
    auto* semibold = FindFont("Inter SemiBold");
    if (!regular)
        regular = ImGui::GetFont();
    if (!semibold)
        semibold = regular;
    faces_ = {{
        {regular, PaintSize(regular, theme.tokens.text("type/value").size_pt * scale, density)},
        {regular, PaintSize(regular, theme.tokens.text("type/caption").size_pt * scale, density)},
        {semibold, PaintSize(semibold, theme.tokens.text("type/section").size_pt * scale, density)},
    }};
}

float Typography::width(std::string_view text, TextRole role) const {
    if (text.empty())
        return 0;
    const auto face = faces_[static_cast<unsigned>(role)];
    DensityScope density(*face.font, density_);
    return face.font->CalcTextSizeA(face.size, FLT_MAX, 0, text.data(), text.data() + text.size()).x;
}

void Typography::label(ImDrawList& draw, PixelGrid grid, Rect bounds, std::string_view text,
    float inset, ImU32 color, TextRole role) const {
    if (text.empty() || bounds.size().x <= inset)
        return;
    const auto face = faces_[static_cast<unsigned>(role)];
    DensityScope density(*face.font, density_);
    const auto* baked = face.font->GetFontBaked(face.size);
    const float ascent = baked->Ascent * face.size / baked->Size;
    // Centre the ascent–descent line box and snap its baseline in device space.
    const float center_top = bounds.min.y + (bounds.size().y - face.size) * 0.5f;
    const auto baseline = grid.snap({bounds.min.x + inset, center_top + ascent});
    const ImVec4 clip(bounds.min.x, bounds.min.y, bounds.max.x, bounds.max.y);
    const auto flags = draw.Flags;
    draw.Flags |= ImDrawListFlags_TextNoPixelSnap;
    draw.AddText(face.font, face.size, {baseline.x, baseline.y - ascent}, color,
        text.data(), text.data() + text.size(), 0, &clip);
    draw.Flags = flags;
}

bool Typography::glyph(ImDrawList& draw, PixelGrid grid, Rect bounds, ImWchar codepoint,
    ImFont* font, ImU32 color) const {
    font = font ? font : lucide_;
    if (!font)
        return false;
    DensityScope density(*font, density_);
    const float size = PaintSize(font, icon_size_, density_);
    auto* baked = font->GetFontBaked(size);
    const auto* glyph = baked->FindGlyphNoFallback(codepoint);
    if (!glyph || !glyph->Visible)
        return false;
    const float k = size / baked->Size;
    const auto center = bounds.min + bounds.size() * 0.5f;
    const auto baseline = grid.snap({center.x - (glyph->X0 + glyph->X1) * 0.5f * k,
        center.y + (baked->Ascent - (glyph->Y0 + glyph->Y1) * 0.5f) * k});
    char utf8[5];
    const int length = ImTextCharToUtf8(utf8, codepoint);
    const auto flags = draw.Flags;
    draw.Flags |= ImDrawListFlags_TextNoPixelSnap;
    draw.AddText(font, size, {baseline.x, baseline.y - baked->Ascent * k}, color, utf8, utf8 + length);
    draw.Flags = flags;
    return true;
}

void Typography::warm(const RegistrySnapshot& snapshot) const {
    const auto warm_text = [&](std::string_view text, TextRole role) {
        if (text.empty())
            return;
        const auto face = faces_[static_cast<unsigned>(role)];
        auto* baked = face.font->GetFontBaked(face.size, density_);
        const char* cursor = text.data();
        const char* end = cursor + text.size();
        while (cursor < end) {
            unsigned int codepoint = 0;
            const int read = ImTextCharFromUtf8(&codepoint, cursor, end);
            if (read <= 0)
                break;
            baked->FindGlyph(static_cast<ImWchar>(codepoint));
            cursor += read;
        }
    };
    const auto warm_icon = [&](const Icon& icon) {
        if (icon.kind_ != Icon::Kind::Glyph)
            return;
        auto* font = icon.font_ ? icon.font_ : lucide_;
        if (font)
            font->GetFontBaked(PaintSize(font, icon_size_, density_), density_)->FindGlyphNoFallback(icon.codepoint_);
    };
    for (const auto& [id, item] : snapshot.items) {
        const auto& desc = item->desc;
        warm_text(desc.label.empty() ? desc.alt_text : desc.label,
            desc.kind == ItemKind::Header ? TextRole::Section : TextRole::Value);
        warm_text(desc.shortcut, TextRole::Caption);
        warm_icon(desc.icon);
    }
    for (const auto& menu : snapshot.layout.menus) {
        warm_text(menu.label, TextRole::Value);
        warm_icon(menu.icon);
        for (const auto& item : menu.entries) {
            if (item.kind == EntryKind::Header)
                warm_text(item.text, TextRole::Section);
        }
    }
    for (const auto& set : snapshot.layout.sets)
        warm_text(set.id, TextRole::Value);
    warm_text("Recent Commands Menu Sets Hotbox Controls Pin Hotbox Menu Rows Zone Ticks", TextRole::Value);
    warm_text("12345678", TextRole::Caption);
}

Painter::Painter(ImDrawList& draw, const Theme& theme, const AtlasAssets& atlas, PixelGrid grid, float scale)
    : draw_(draw), theme_(theme), atlas_(atlas), grid_(grid), scale_(scale),
      rounding_(theme.shape == ShapeSetting::Rounded ? 1.0f : 0.0f) {}

template<class Draw>
void Painter::device_geometry(const Draw& draw) {
    // ImGui tessellates and anti-aliases in the input coordinate space. Give it
    // device pixels, then convert just the vertices it emitted back to ImGui space.
    const int first = draw_.VtxBuffer.Size;
    draw();
    for (int i = first; i < draw_.VtxBuffer.Size; ++i)
        draw_.VtxBuffer[i].pos = Vec(grid_.logical(PointOf(draw_.VtxBuffer[i].pos)));
}

ImU32 Painter::color(ImVec4 value) { return ImGui::ColorConvertFloat4ToU32(value); }

ImU32 Painter::ink(ImVec4 value) const {
    value.w *= opacity_;
    return color(value);
}

ImU32 Painter::tint(ImU32 value) const {
    const auto alpha = static_cast<ImU32>(std::round(static_cast<float>(value >> IM_COL32_A_SHIFT) * opacity_));
    return (value & ~IM_COL32_A_MASK) | (alpha << IM_COL32_A_SHIFT);
}

float Painter::radius(float pt) const { return pt * rounding_; }

void Painter::rounded_rect(Rect rect, float r, ImU32 fill, ImU32 stroke, float border_px) {
    fill = tint(fill);
    stroke = tint(stroke);
    const auto min = Round(grid_.physical(rect.min));
    const auto max = Round(grid_.physical(rect.max));
    if (max.x <= min.x || max.y <= min.y)
        return;
    const float rounding = std::min(r * scale_ * grid_.density.x,
        std::min(max.x - min.x, max.y - min.y) * 0.5f);
    device_geometry([&] {
        if (fill & IM_COL32_A_MASK)
            draw_.AddRectFilled(Vec(min), Vec(max), fill, rounding);
        if ((stroke & IM_COL32_A_MASK) && max.x - min.x > border_px && max.y - min.y > border_px) {
            const Point inset{border_px / 2, border_px / 2};
            draw_.PathRect(Vec(min + inset), Vec(max - inset), std::max(0.0f, rounding - border_px / 2));
            draw_.PathStroke(stroke, border_px, ImDrawFlags_Closed);
        }
    });
}

void Painter::line(Point from, Point to, ImU32 ink, float pixels) {
    ink = tint(ink);
    if (!(ink & IM_COL32_A_MASK))
        return;
    const auto a = PixelCenter(grid_.physical(from));
    const auto b = PixelCenter(grid_.physical(to));
    device_geometry([&] {
        if (pixels == 1 && a.x == b.x) {
            const float low = std::min(a.y, b.y) - 0.5f;
            const float high = std::max(low + 1, std::max(a.y, b.y) - 0.5f);
            draw_.AddRectFilled({a.x - 0.5f, low}, {a.x + 0.5f, high}, ink);
        } else if (pixels == 1 && a.y == b.y) {
            const float low = std::min(a.x, b.x) - 0.5f;
            const float high = std::max(low + 1, std::max(a.x, b.x) - 0.5f);
            draw_.AddRectFilled({low, a.y - 0.5f}, {high, a.y + 0.5f}, ink);
        } else {
            const ImVec2 points[2]{Vec(a), Vec(b)};
            draw_.AddPolyline(points, 2, ink, pixels);
        }
    });
}

void Painter::shadow(Rect rect, const ShadowSlice& slice, ImU32 ink, float offset) {
    ink = tint(ink);
    if (!(ink & IM_COL32_A_MASK))
        return;
    const auto image = atlas_.image(slice.rect);
    if (!image)
        return;
    const auto translation = Point{0, offset * scale_};
    const auto min = Round(grid_.physical(rect.min + translation));
    const auto max = Round(grid_.physical(rect.max + translation));
    const auto grid = [&](float low, float high) {
        const float inset = std::min(static_cast<float>(slice.edge - slice.margin), (high - low) * 0.5f);
        return std::array{low - slice.margin, low + inset, high - inset, high + slice.margin};
    };
    const auto x = grid(min.x, max.x);
    const auto y = grid(min.y, max.y);
    const float split = static_cast<float>(slice.edge) / static_cast<float>(2 * slice.edge + 3);
    constexpr float one = 1.0f;
    const std::array uv{0.0f, split, one - split, one};
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t col = 0; col < 3; ++col) {
            if (x[col] == x[col + 1] || y[row] == y[row + 1])
                continue;
            draw_.AddImage(image.texture, Vec(grid_.logical({x[col], y[row]})),
                Vec(grid_.logical({x[col + 1], y[row + 1]})),
                {std::lerp(image.uv0.x, image.uv1.x, uv[col]), std::lerp(image.uv0.y, image.uv1.y, uv[row])},
                {std::lerp(image.uv0.x, image.uv1.x, uv[col + 1]), std::lerp(image.uv0.y, image.uv1.y, uv[row + 1])}, ink);
        }
    }
}

void Painter::scrim(Rect rect, float opacity) {
    auto ink = theme_.colors.scrim;
    ink.w *= std::clamp(opacity, 0.0f, 1.0f);
    rounded_rect(rect, 0, color(ink));
}

void Painter::shell(Rect rect) {
    auto shadow_color = theme_.colors.pill_shadow;
    shadow_color.w *= 1 - rounding_;
    shadow(rect, atlas_.shell(false), color(shadow_color), 0);
    shadow_color = theme_.colors.pill_shadow;
    shadow_color.w *= rounding_;
    shadow(rect, atlas_.shell(true), color(shadow_color), 0);
    rounded_rect(rect, radius(8), color(theme_.colors.surface), color(theme_.colors.edge));
}

void Painter::menu(Rect rect) {
    auto shadow_color = theme_.colors.shadow;
    shadow_color.w *= 1 - rounding_;
    shadow(rect, atlas_.menu(false), color(shadow_color), 8);
    shadow_color = theme_.colors.shadow;
    shadow_color.w *= rounding_;
    shadow(rect, atlas_.menu(true), color(shadow_color), 8);
    rounded_rect(rect, radius(6), color(theme_.colors.surface), color(theme_.colors.edge));
}

void Painter::target(Rect rect, ImVec4 fill, float outline) {
    auto edge = theme_.colors.edge;
    edge.w *= outline;
    rounded_rect(rect, radius(4), color(fill), color(edge));
}

// The focus ring is a 2 pt inside border, as the design system's focus styles.
void Painter::focus(Rect rect) {
    rounded_rect(rect, radius(4), 0, color(theme_.colors.focus), 2 * scale_ * grid_.density.x);
}
void Painter::key_cap(Rect rect) {
    rounded_rect(rect, radius(4), color(theme_.colors.key_fill), color(theme_.colors.key_edge));
}
void Painter::caret(Point from, Point to) { line(from, to, color(theme_.colors.ink)); }
void Painter::separator(Point from, Point to) { line(from, to, color(theme_.colors.edge)); }
void Painter::option_box(Rect rect, ImU32 ink) { rounded_rect(rect, radius(1), 0, ink); }

void Painter::check_mark(Rect rect, bool enabled) {
    const auto p = rect.min + rect.size() * 0.5f;
    const ImVec2 points[]{Vec(PixelCenter(grid_.physical(p + Point{-4, 0} * scale_))),
        Vec(PixelCenter(grid_.physical(p + Point{-1, 3} * scale_))),
        Vec(PixelCenter(grid_.physical(p + Point{5, -4} * scale_)))};
    device_geometry([&] { draw_.AddPolyline(points, 3, ink(enabled ? theme_.colors.accent : theme_.colors.disabled), 1.5f); });
}

void Painter::zone_tick(Point from, Point to) { line(from, to, color(theme_.colors.ring)); }
void Painter::rubber_band(Point from, Point to) { line(from, to, color(theme_.colors.focus)); }

void Painter::press_point(Point pivot) {
    const auto center = PixelCenter(grid_.physical(pivot));
    const float r = 3 * scale_ * grid_.density.x;
    device_geometry([&] {
        draw_.AddCircleFilled(Vec(center), r, ink(theme_.colors.surface));
        draw_.AddCircle(Vec(center), r, ink(theme_.colors.focus));
    });
}

void Painter::ink_mark(std::span<const Point> stroke) {
    if (stroke.size() < 2)
        return;
    std::array<ImVec2, 4096> output;
    std::size_t count = 1;
    Point prev = grid_.physical(stroke.front());
    output[0] = Vec(prev);
    float remaining = 2;
    for (std::size_t i = 1; i < stroke.size() && count < output.size(); ++i) {
        const auto b = grid_.physical(stroke[i - 1]);
        const auto c = grid_.physical(stroke[i]);
        auto a = i > 1 ? grid_.physical(stroke[i - 2]) : b + (b - c);
        auto d = i + 1 < stroke.size() ? grid_.physical(stroke[i + 1]) : c + (c - b);
        if (IsTurn(a, b, c))
            a = b + (b - c);
        const bool corner = IsTurn(b, c, d);
        if (corner)
            d = c + (c - b);
        const int samples = static_cast<int>(std::clamp(std::ceil(Length(c - b)), 1.0f, 256.0f));
        for (int step = 1; step <= samples && count < output.size(); ++step) {
            const auto current = CatmullRom(a, b, c, d, static_cast<float>(step) / samples);
            float distance = Length(current - prev);
            while (distance >= remaining && count < output.size()) {
                prev = prev + (current - prev) * (remaining / distance);
                output[count++] = Vec(prev);
                distance -= remaining;
                remaining = 2;
            }
            remaining -= distance;
            prev = current;
        }
        if ((corner || i + 1 == stroke.size()) && count < output.size() &&
            PointOf(output[count - 1]) != c) {
            output[count++] = Vec(c);
            remaining = 2;
        }
    }
    device_geometry([&] { draw_.AddPolyline(output.data(), static_cast<int>(count), ink(theme_.colors.trail), 1.5f); });
}

void Painter::icon(const Typography& type, Rect slot, const Icon& icon, bool enabled) {
    const auto ink = enabled && icon.has_color_ ? icon.color_
        : color(enabled ? theme_.colors.ink : theme_.colors.disabled);
    switch (icon.kind_) {
    case Icon::Kind::None: break;
    case Icon::Kind::Glyph:
        type.glyph(draw_, grid_, slot, icon.codepoint_, icon.font_, tint(ink));
        break;
    case Icon::Kind::Svg: {
        const auto image = atlas_.svg(icon, enabled);
        if (image) {
            const ImU32 image_color = icon.tintable_ || icon.has_color_ ? ink
                : enabled ? IM_COL32_WHITE : IM_COL32(255, 255, 255, 102);
            draw_.AddImage(image.texture, Vec(grid_.snap(slot.min)), Vec(grid_.snap(slot.max)),
                image.uv0, image.uv1, tint(image_color));
        }
        break;
    }
    case Icon::Kind::Texture: {
        if (icon.size_.x <= 0 || icon.size_.y <= 0)
            break;
        const float scale = std::min({slot.size().x / icon.size_.x, slot.size().y / icon.size_.y, scale_});
        const auto extent = PointOf(icon.size_) * scale;
        const auto min = slot.min + (slot.size() - extent) * 0.5f;
        const ImU32 image_color = icon.tintable_ || (enabled && icon.has_color_) ? ink
            : enabled ? IM_COL32_WHITE : IM_COL32(255, 255, 255, 102);
        draw_.AddImage(icon.texture_, Vec(grid_.snap(min)), Vec(grid_.snap(min + extent)), icon.uv0_, icon.uv1_, tint(image_color));
        break;
    }
    case Icon::Kind::Monogram:
        monogram(type, slot, icon.text_, enabled);
        break;
    }
}

void Painter::monogram(const Typography& type, Rect slot, std::string_view label, bool enabled) {
    if (label.empty())
        return;
    unsigned int codepoint = 0;
    const char* end = label.data() + label.size();
    auto* cursor = label.data();
    for (int count = 0; count < 2 && cursor < end; ++count) {
        const auto length = ImTextCharFromUtf8(&codepoint, cursor, end);
        if (length <= 0)
            break;
        cursor += length;
    }
    label = label.substr(0, static_cast<std::size_t>(cursor - label.data()));
    rounded_rect(slot, radius(3), color(theme_.colors.key_fill), color(theme_.colors.key_edge));
    const float inset = std::max(0.0f, (slot.size().x - type.width(label, TextRole::Caption)) * 0.5f);
    type.label(draw_, grid_, slot, label, inset,
        ink(enabled ? theme_.colors.key_ink : theme_.colors.disabled), TextRole::Caption);
}

} // namespace hotbox::detail

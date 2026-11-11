#include "overlay.hpp"
#include "log.hpp"

#include <codepoints.hpp>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>

#ifdef HOTBOX_WITH_SPDLOG
#include <spdlog/spdlog.h>
#endif

namespace hotbox::detail {
namespace {

constexpr std::array<std::string_view, 6> panel_ids{"view", "shading", "lighting", "show", "renderer", "panels"};
constexpr float row_height = 36;
constexpr float row_pitch = 40;
constexpr float center_row_pitch = 46; // A 10 pt gap separates the centre row from its neighbours.
constexpr float row_gap = 4;
constexpr float menu_row_height = 22;

ImVec2 Vec(Point point) { return {point.x, point.y}; }

bool Predicate(const std::function<bool()>& fn, bool default_value, std::string_view id) {
    if (!fn)
        return default_value;
    try {
        return fn();
    } catch (...) {
#ifdef HOTBOX_WITH_SPDLOG
        SPDLOG_LOGGER_ERROR(logger(), "Predicate failed for hotbox command '{}'", id);
#else
        (void)id;
#endif
        return false;
    }
}

bool Contains(const MenuSet& set, std::string_view id) {
    return std::ranges::find(set.menus, id) != set.menus.end();
}

const Icon* Artwork(const Control& control) {
    if (control.item && AtlasAssets::has_icon(control.item->icon))
        return &control.item->icon;
    return control.menu_icon && AtlasAssets::has_icon(*control.menu_icon) ? control.menu_icon : nullptr;
}

bool AutoMonogram(const Control& control, bool enabled) {
    return enabled && control.item && control.item->label.empty() && !control.label.empty();
}

bool HasArtwork(const Control& control, bool monograms) {
    return Artwork(control) || AutoMonogram(control, monograms);
}

bool DividerBefore(std::string_view previous, std::string_view next) {
    return (previous == "cache" && next == "help") ||
           (previous == "mesh-display" && next == "curves");
}

Rect IconSlot(Rect rect, float inset, float scale) {
    const Point min{rect.min.x + inset, rect.min.y + (rect.size().y - 16 * scale) / 2};
    return {min, min + Point{16 * scale, 16 * scale}};
}

enum class TargetState { Rest, Hover, Selected, SelectedHover, Pressed };

TargetState State(bool enabled, bool selected, bool hovered, bool pressed) {
    if (!enabled) return TargetState::Rest;
    if (hovered && pressed) return TargetState::Pressed;
    if (selected) return hovered ? TargetState::SelectedHover : TargetState::Selected;
    return hovered ? TargetState::Hover : TargetState::Rest;
}

Rect Translate(Rect rect, Point delta) { return {rect.min + delta, rect.max + delta}; }

ImVec4 Blend(ImVec4 from, ImVec4 to, float amount) {
    if (amount <= 0)
        return from;
    if (amount >= 1)
        return to;
    return iam_get_blended_color(from, to, amount, iam_col_oklab);
}

float Visibility(Motion& motion, ImGuiID owner, const char* channel, bool visible,
    float duration, int enter, int exit, bool cut = false) {
    return motion.tween(owner, channel, visible ? 1.0f : 0.0f, duration * (visible ? 1.0f : 0.7f),
        visible ? enter : exit, 0, cut);
}

/// Distance in points from the centre row's midline to the midline of `row`.
float RowOffset(int row) {
    if (row == 0)
        return 0;
    const float distance = center_row_pitch + static_cast<float>(std::abs(row) - 1) * row_pitch;
    return row < 0 ? -distance : distance;
}

float Delay(float stagger, int step) {
    return stagger * static_cast<float>(std::min(step, 6));
}

} // namespace

Control Resolve(const Registry& registry, const Layout& layout, std::string_view id, bool submenu) {
    const auto* item = registry.find(id);
    const auto* menu = layout.find_menu(id);
    if (item && !Predicate(item->is_visible, true, id))
        return {ControlKind::Command, {}, {}};
    if (submenu || (item && item->kind == ItemKind::Submenu) || (!item && menu)) {
        if (!menu)
            return {ControlKind::Command, {}, {}};
        const auto label = item && !item->label.empty() ? std::string_view(item->label) : menu->label;
        Control control{ControlKind::Submenu, menu->id, label, {}, item,
            !item || Predicate(item->is_enabled, true, id)};
        control.menu_icon = &menu->icon;
        return control;
    }
    if (!item)
        return {ControlKind::Command, {}, {}};
    if (item->kind == ItemKind::Separator)
        return {ControlKind::Separator, id, {}};
    const auto label = item->label.empty() ? std::string_view(item->alt_text) : item->label;
    if (item->kind == ItemKind::Header)
        return {ControlKind::Header, id, label};
    return {ControlKind::Command, item->id, label, {}, item,
        Predicate(item->is_enabled, true, id), Predicate(item->is_checked, false, id)};
}

void OverlayView::reserve(const Layout& layout) {
    std::size_t entries = layout.sets.size() + 3;
    for (const auto& menu : layout.menus)
        entries += menu.entries.size();
    for (const auto& slot : layout.marking)
        entries += slot.overflow.size();
    titles_.reserve(layout.menus.size() + 3);
    strips_.reserve(layout.menus.size());
    dividers_.reserve(2);
    shells_.reserve(layout.menus.size() + 3);
    popup_items_.reserve(entries);
    popups_.reserve(layout.menus.size() + 1);
}

void OverlayView::begin(const Layout& layout, ViewSettings settings, const Typography& type) {
    reserve(layout);
    settings_ = settings;
    type_ = type;
    titles_.clear();
    strips_.clear();
    shells_.clear();
    dividers_.clear();
    popup_items_.clear();
    popups_.clear();
    mark_boxes_ = {};
    ticks_top_ = settings.origin.y - row_height * settings.scale / 2;
    ticks_bottom_ = settings.origin.y + row_height * settings.scale / 2;
    const auto active = std::ranges::find(layout.sets, layout.active_set, &MenuSet::id);
    if (settings.rows) {
        const auto append = [&](const Menu& menu) {
            titles_.push_back({ControlKind::Menu, menu.id, menu.label});
            titles_.back().menu_icon = &menu.icon;
        };
        for (const auto& menu : layout.menus) {
            if (menu.top_level && std::ranges::find(panel_ids, menu.id) == panel_ids.end() &&
                std::ranges::none_of(layout.sets, [&](const auto& set) { return Contains(set, menu.id); }))
                append(menu);
        }
        const auto common_end = titles_.size();
        for (const auto& menu : layout.menus) {
            if (menu.top_level && std::ranges::find(panel_ids, menu.id) != panel_ids.end() &&
                std::ranges::none_of(layout.sets, [&](const auto& set) { return Contains(set, menu.id); }))
                append(menu);
        }
        const auto panel_end = titles_.size();
        const bool has_panel = panel_end != common_end;
        if (common_end)
            strips_.push_back({0, common_end, has_panel ? -2 : -1});
        if (has_panel)
            strips_.push_back({common_end, panel_end, -1});
        if (active != layout.sets.end()) {
            std::size_t start = titles_.size();
            float width = row_gap * settings.scale;
            const float limit = std::min(700 * settings.scale, settings.work.size().x);
            int row = 1;
            for (const auto& id : active->menus) {
                const auto* menu = layout.find_menu(id);
                if (!menu)
                    continue;
                const float artwork = AtlasAssets::has_icon(menu->icon) ? 20 * settings.scale : 0;
                const bool divider = titles_.size() > start && DividerBefore(titles_.back().id, id);
                const float cell = std::max(28 * settings.scale, type_.width(menu->label) + 20 * settings.scale + artwork);
                if (divider)
                    width += 5 * settings.scale;
                if (width + cell + row_gap * settings.scale > limit && titles_.size() > start) {
                    strips_.push_back({start, titles_.size(), row++});
                    start = titles_.size();
                    width = row_gap * settings.scale;
                }
                width += cell + row_gap * settings.scale;
                append(*menu);
            }
            if (titles_.size() > start)
                strips_.push_back({start, titles_.size(), row});
        }
    }
    const int above = strips_.empty() ? 0 : std::max(0, -strips_.front().row);
    const int below = strips_.empty() ? 0 : std::max(0, strips_.back().row);
    const float upper = (row_height / 2 - RowOffset(-above)) * settings.scale;
    const float lower = (row_height / 2 + RowOffset(below)) * settings.scale;
    const float min_y = settings.work.min.y + upper;
    const float max_y = settings.work.max.y - lower;
    const float center_y = min_y <= max_y ? std::clamp(settings.origin.y, min_y, max_y)
                                        : (min_y + max_y) * 0.5f;
    ticks_top_ = center_y - upper;
    ticks_bottom_ = center_y + lower;
    for (const auto& strip : strips_)
        layout_strip(strip, center_y);

    center_start_ = titles_.size();
    titles_.push_back({ControlKind::Recent, {}, "Recent Commands", {}, nullptr, false});
    titles_.push_back({ControlKind::Sets, {}, active == layout.sets.end() ? std::string_view("Menu Sets") : active->id});
    titles_.push_back({ControlKind::Controls, {}, "Hotbox Controls"});
    layout_center(center_y);
}

void OverlayView::layout_strip(Strip strip, float center_y) {
    const float s = settings_.scale;
    float width = row_gap * s;
    for (std::size_t i = strip.start; i < strip.end; ++i) {
        const float artwork = HasArtwork(titles_[i], settings_.monograms) ? 20 * s : 0;
        const float target = std::max(28 * s, type_.width(titles_[i].label) + 20 * s + artwork);
        titles_[i].rect.max.x = target;
        width += target + row_gap * s;
        if (i > strip.start && DividerBefore(titles_[i - 1].id, titles_[i].id))
            width += 5 * s;
    }
    const float y = center_y + RowOffset(strip.row) * s - row_height * s / 2;
    const Point min{settings_.origin.x - width / 2, y};
    const auto shell = ClampRect({min, min + Point{width, row_height * s}},
        {{settings_.work.min.x, y}, {settings_.work.max.x, y + row_height * s}});
    shells_.push_back({shell, strip.row});
    float x = shell.min.x + row_gap * s;
    for (std::size_t i = strip.start; i < strip.end; ++i) {
        const float target = titles_[i].rect.max.x;
        if (i > strip.start && DividerBefore(titles_[i - 1].id, titles_[i].id)) {
            if (x + 5 * s < shell.max.x)
                dividers_.emplace_back(Point{x, y + row_height * s / 2}, strip.row);
            x += 5 * s;
        }
        titles_[i].rect = {{x, y + row_gap * s},
            {std::min(x + target, shell.max.x - row_gap * s), y + (row_height - row_gap) * s}};
        titles_[i].row = strip.row;
        x += target + row_gap * s;
    }
}

void OverlayView::layout_center(float center_y) {
    const float s = settings_.scale;
    std::array<float, 3> widths{};
    for (std::size_t i = 0; i < widths.size(); ++i)
        widths[i] = std::max(36 * s, type_.width(titles_[center_start_ + i].label) + 28 * s);
    const float y = center_y - row_height * s / 2;
    const float gap = 20 * s;
    const float left = widths[0] + gap + widths[1] / 2;
    const float right = widths[2] + gap + widths[1] / 2;
    const float min_x = settings_.work.min.x + left;
    const float max_x = settings_.work.max.x - right;
    const float anchor = min_x <= max_x ? std::clamp(settings_.origin.x, min_x, max_x)
                                      : (min_x + max_x) / 2;
    float x = anchor - left;
    for (std::size_t i = 0; i < widths.size(); ++i) {
        const Rect shell{{x, y}, {x + widths[i], y + row_height * s}};
        shells_.push_back({shell, 0});
        titles_[center_start_ + i].rect = {shell.min + Point{row_gap * s, row_gap * s},
            shell.max - Point{row_gap * s, row_gap * s}};
        x += widths[i] + gap;
    }
}

void OverlayView::marking(Point pivot, const std::array<Control, 8>& choices) {
    std::array<float, 8> widths{};
    for (std::size_t i = 0; i < widths.size(); ++i) {
        if (!choices[i].id.empty())
            widths[i] = std::max(42 * settings_.scale, type_.width(choices[i].label) +
                (HasArtwork(choices[i], settings_.monograms) ? 56 : 36) * settings_.scale);
    }
    mark_boxes_ = LayoutBoxes(pivot, widths, settings_.work, 36 * settings_.scale,
        92 * settings_.scale, row_gap * settings_.scale);
}

void OverlayView::popup(const Registry& registry, const Layout& layout, ControlKind kind,
    std::string_view id, Rect anchor_rect, std::size_t level, std::span<const CommandId> overflow, ImGuiID scope) {
    const auto start = popup_items_.size();
    if (kind == ControlKind::Menu || kind == ControlKind::Submenu) {
        if (const auto* menu = layout.find_menu(id)) {
            for (const auto& entry : menu->entries) {
                if (entry.kind == EntryKind::Separator) {
                    popup_items_.push_back({ControlKind::Separator, {}, {}});
                } else if (entry.kind == EntryKind::Header) {
                    popup_items_.push_back({ControlKind::Header, {}, entry.text});
                } else {
                    auto item = Resolve(registry, layout, entry.text, entry.kind == EntryKind::Submenu);
                    if (!item.id.empty())
                        popup_items_.push_back(item);
                }
            }
        }
    } else if (kind == ControlKind::Sets) {
        for (const auto& set : layout.sets)
            popup_items_.push_back({ControlKind::ChooseSet, set.id, set.id, {}, nullptr, true,
                set.id == layout.active_set});
    } else if (kind == ControlKind::Controls) {
        popup_items_.push_back({ControlKind::Pin, {}, "Pin Hotbox", {}, nullptr, true, settings_.pinned});
        popup_items_.push_back({ControlKind::Rows, {}, "Menu Rows", {}, nullptr, true, settings_.rows});
        popup_items_.push_back({ControlKind::Ticks, {}, "Zone Ticks", {}, nullptr, true, settings_.ticks});
    } else if (kind == ControlKind::Overflow) {
        if (const auto* menu = id.empty() ? nullptr : layout.find_menu(id)) {
            std::size_t direction = 0;
            for (const auto& entry : menu->entries) {
                if (entry.kind != EntryKind::Item && entry.kind != EntryKind::Submenu)
                    continue;
                if (direction++ < 8)
                    continue;
                auto item = Resolve(registry, layout, entry.text, entry.kind == EntryKind::Submenu);
                if (!item.id.empty())
                    popup_items_.push_back(item);
            }
        } else {
            for (const auto& command : overflow) {
                auto item = Resolve(registry, layout, command);
                if (!item.id.empty())
                    popup_items_.push_back(item);
            }
        }
    }

    const float s = settings_.scale;
    const bool icons = std::any_of(popup_items_.begin() + static_cast<std::ptrdiff_t>(start), popup_items_.end(),
        [&](const Control& item) { return HasArtwork(item, settings_.monograms); });
    const float icon_column = icons ? 20 * s : 0;
    float width = 120 * s;
    float height = 8 * s;
    for (std::size_t i = start; i < popup_items_.size(); ++i) {
        const auto& item = popup_items_[i];
        const auto role = item.kind == ControlKind::Header ? TextRole::Section : TextRole::Value;
        const float shortcut = item.item && !item.item->shortcut.empty()
            ? type_.width(item.item->shortcut, TextRole::Caption) + 16 * s : 0;
        const float option = item.item && item.item->on_option ? 22 * s : 0;
        const float chrome = (item.kind == ControlKind::Submenu ? 18 : 0) * s;
        width = std::max(width, type_.width(item.label, role) + 44 * s + shortcut + option + chrome + icon_column);
        height += (item.kind == ControlKind::Separator ? 9 : menu_row_height) * s;
    }
    if (popup_items_.size() == start) {
        popup_items_.push_back({ControlKind::Header, {}, "No commands"});
        height += menu_row_height * s;
    }
    // A dropdown sits 4 pt below the title's shell (the target is inset 4 pt in it) and 16 pt left of
    // the target, so its label column (inset 4 + check column 22) lines up with the title's label (padding 10).
    // A submenu starts at its parent's edge, raised by the row inset so its first row lines up with the parent row.
    constexpr float shell_inset = 4, dropdown_gap = 4, label_shift = 16;
    Point position = level ? Point{anchor_rect.max.x + shell_inset * s, anchor_rect.min.y - shell_inset * s}
        : Point{anchor_rect.min.x - label_shift * s, anchor_rect.max.y + (shell_inset + dropdown_gap) * s};
    if (level && position.x + width > settings_.work.max.x)
        position.x = anchor_rect.min.x - width - shell_inset * s;
    const float above_y = anchor_rect.min.y - height - (shell_inset + dropdown_gap) * s;
    if (!level && position.y + height > settings_.work.max.y && above_y >= settings_.work.min.y)
        position.y = above_y;
    const Rect box = ClampRect({position, position + Point{width, height}}, settings_.work);
    const auto key = static_cast<ImGuiID>(kind) + (static_cast<ImGuiID>(level) << 8);
    if (level && !popups_.empty())
        scope = popups_.back().id;
    scope = ImHashData(&key, sizeof(key), scope);
    const ImGuiID panel = id.empty() ? scope : ImHashData(id.data(), id.size(), scope);
    popups_.push_back({box, panel, !level && box.max.y <= anchor_rect.min.y,
        level && box.max.x <= anchor_rect.min.x, icon_column});
    float y = box.min.y + 4 * s;
    for (std::size_t i = start; i < popup_items_.size(); ++i) {
        auto& item = popup_items_[i];
        const float h = (item.kind == ControlKind::Separator ? 9 : menu_row_height) * s;
        item.level = level;
        item.rect = {{box.min.x + 4 * s, y},
                     {box.max.x - 4 * s, std::min(y + h, box.max.y - 4 * s)}};
        y += h;
    }
}

const Control* OverlayView::title(Point pointer) const {
    if (!settings_.work.contains(pointer))
        return nullptr;
    const auto item = std::ranges::find_if(titles_, [&](const auto& title) { return title.rect.contains(pointer); });
    return item == titles_.end() ? nullptr : &*item;
}

const Control* OverlayView::popup_item(Point pointer) const {
    if (!settings_.work.contains(pointer))
        return nullptr;
    for (auto it = popup_items_.rbegin(); it != popup_items_.rend(); ++it) {
        if (it->rect.contains(pointer))
            return &*it;
    }
    return nullptr;
}

const Control* OverlayView::anchor(ControlKind kind, std::string_view id, std::size_t level) const {
    const auto& controls = level ? popup_items_ : titles_;
    const auto match = std::ranges::find_if(controls, [&](const auto& control) {
        return control.kind == kind && control.id == id && control.level == (level ? level - 1 : 0);
    });
    return match == controls.end() ? nullptr : &*match;
}

std::optional<Dir> OverlayView::mark_at(Point pointer) const {
    for (std::size_t i = 0; i < mark_boxes_.size(); ++i) {
        if (mark_boxes_[i] && mark_boxes_[i]->contains(pointer))
            return static_cast<Dir>(i);
    }
    return {};
}

Rect OverlayView::overflow_anchor(Point pivot) const {
    if (const auto& south = mark_boxes_[static_cast<unsigned>(Dir::S)])
        return *south;
    pivot = pivot + Point{0, 96 * settings_.scale};
    return {pivot - Point{48 * settings_.scale, 0}, pivot + Point{48 * settings_.scale, 0}};
}

bool OverlayView::over_popup(Point point) const {
    return std::ranges::any_of(popups_, [&](const auto& popup) { return popup.rect.contains(point); });
}

bool OverlayView::on_chrome(Point point) const {
    return over_popup(point) || std::ranges::any_of(shells_, [&](const auto& shell) { return shell.rect.contains(point); });
}

void OverlayView::draw(ImDrawList& draw, const Theme& theme, const AtlasAssets& atlas, PixelGrid grid,
    const OverlayAnimation& animation, Point pointer, bool pressed, float scrim, bool scrim_visible,
    bool show_rows, const Control* active_title,
    const std::array<Control, 8>* choices, std::optional<Dir> armed, Point pivot,
    std::span<const Point> ink, OverlayLayer layer, std::size_t first_popup) const {
    const auto& color = theme.colors;
    const auto& timing = theme.motion;
    auto& motion = animation.motion;
    constexpr float invisible = 0.001f;
    const float s = settings_.scale;
    Painter paint(draw, theme, atlas, grid, s);
    paint.rounding(animation.rounding);
    const auto label = [&](Rect rect, std::string_view text, float inset, ImVec4 ink_color,
        TextRole role = TextRole::Value) {
        type_.label(draw, grid, rect, text, inset, paint.ink(ink_color), role);
    };
    const auto artwork = [&](const Control& control, Rect rect, float inset) {
        if (rect.size().x < inset + 16 * s)
            return 0.0f;
        const auto slot = IconSlot(rect, inset, s);
        if (const auto* icon = Artwork(control))
            paint.icon(type_, slot, *icon, control.enabled);
        else if (AutoMonogram(control, settings_.monograms))
            paint.monogram(type_, slot, control.label, control.enabled);
        else
            return 0.0f;
        return 20 * s;
    };
    const auto target = [&](Rect rect, const Control& item, ImGuiID scope, TargetState state) {
        const auto kind = static_cast<ImGuiID>(item.kind);
        const auto owner = motion.owner(item.id.empty() ? item.label : item.id, ImHashData(&kind, sizeof(kind), scope));
        const auto amount = [&](const char* channel, bool active) {
            return motion.tween(owner, channel, active ? 1.0f : 0.0f, timing.hover,
                iam_ease_out_quad, 0, animation.cut || !item.enabled);
        };
        const float hover = amount("hover", state == TargetState::Hover || state == TargetState::SelectedHover);
        const float press = amount("press", state == TargetState::Pressed);
        const float selection = amount("selection", state == TargetState::Selected || state == TargetState::SelectedHover);
        auto clear = color.pill_hover;
        clear.w = 0;
        const auto neutral = Blend(clear, color.pill_hover, hover);
        paint.target(rect, Blend(Blend(neutral, color.pill_pressed, press), color.sector, selection), hover * selection);
        return selection;
    };
    const auto row_amount = [&](int row) {
        const bool ready = animation.cut || !motion.enabled() ||
            animation.rows_age >= Delay(timing.stagger, std::abs(row));
        if (show_rows && !ready)
            motion.keep_rendering();
        return Visibility(motion, motion.owner("rows", static_cast<ImGuiID>(row)), "reveal",
            show_rows && ready, timing.section, iam_ease_in_out_quad, iam_ease_in_out_quad, animation.cut);
    };
    const auto row_shift = [&](int row, float amount) {
        const float direction = static_cast<float>((row > 0) - (row < 0));
        return Point{0, direction * 4 * s * (1 - amount)};
    };

    draw.PushClipRect(Vec(settings_.work.min), Vec(settings_.work.max), true);
    if (layer == OverlayLayer::All) {
        paint.scrim(settings_.work, scrim * Visibility(motion, motion.owner("scrim"), "opacity",
            scrim_visible, timing.popup, iam_ease_out_cubic, iam_ease_in_cubic, animation.cut));
        for (const auto& shell : shells_) {
            const float amount = row_amount(shell.row);
            if (amount <= invisible)
                continue;
            paint.opacity(amount);
            paint.shell(Translate(shell.rect, row_shift(shell.row, amount)));
        }
        for (const auto& [position, row] : dividers_) {
            const float amount = row_amount(row);
            const auto center = position + row_shift(row, amount);
            paint.opacity(amount);
            paint.separator(center - Point{0, 8 * s}, center + Point{0, 8 * s});
        }
        for (std::size_t index = 0; index < titles_.size(); ++index) {
            const auto& title = titles_[index];
            const float amount = row_amount(title.row);
            if (title.rect.size().x <= 0 || amount <= invisible)
                continue;
            const auto rect = Translate(title.rect, row_shift(title.row, amount));
            paint.opacity(amount);
            target(rect, title, motion.owner("title", static_cast<ImGuiID>(index)), State(title.enabled, &title == active_title,
                animation.pointer_live && title.rect.contains(pointer), pressed));
            const auto ink_color = title.kind == ControlKind::Sets ? color.accent
                : title.enabled ? color.pill_ink : color.disabled;
            label(rect, title.label, 10 * s + artwork(title, rect, 10 * s), ink_color);
        }
        if (settings_.ticks) {
            const bool ready = animation.cut || !motion.enabled() ||
                animation.rows_age >= timing.section + Delay(timing.stagger, 6);
            if (show_rows && !ready)
                motion.keep_rendering();
            const float amount = Visibility(motion, motion.owner("ticks"), "opacity", show_rows && ready,
                timing.hover, iam_ease_out_quad, iam_ease_in_quad, animation.cut);
            if (amount > invisible) {
                paint.opacity(amount);
                for (const float x_dir : {-1.0f, 1.0f}) {
                    for (const float y_dir : {-1.0f, 1.0f}) {
                        const float near_y = (y_dir < 0 ? ticks_top_ : ticks_bottom_) + y_dir * 6 * s;
                        const Point start{settings_.origin.x + x_dir * std::abs(near_y - settings_.origin.y), near_y};
                        paint.zone_tick(start, start + Point{x_dir, y_dir} * (14.1421356f * s));
                    }
                }
            }
        }
    }

    paint.opacity(1);
    paint.ink_mark(ink);
    if (layer != OverlayLayer::Popups && choices) {
        if (animation.mark_live) {
            paint.rubber_band(pivot, pointer);
            paint.press_point(pivot);
        }
        for (std::size_t i = 0; i < mark_boxes_.size(); ++i) {
            if (!mark_boxes_[i])
                continue;
            const auto& choice = (*choices)[i];
            const bool invoked = !animation.invoked.empty() && animation.invoked == choice.id;
            const bool selected = choice.enabled && (invoked || armed == static_cast<Dir>(i));
            const int difference = std::abs(static_cast<int>(i) - static_cast<int>(animation.first_mark));
            const int stagger = std::min(difference, 8 - difference);
            const float delay = Delay(timing.stagger, stagger);
            const bool ready = animation.cut || !motion.enabled() || animation.marking_age >= delay;
            if (animation.mark_live && !ready)
                motion.keep_rendering();
            const auto scope = ImHashData(&i, sizeof(i), animation.marking_scope);
            const auto owner = motion.owner(choice.id, scope);
            const bool shaking = !animation.shake_id.empty() && choice.id == animation.shake_id;
            const bool hold = animation.hold_scene || (invoked && animation.hold_invoked) || shaking;
            const float amount = Visibility(motion, owner, "marking reveal",
                (animation.mark_live && ready) || hold, timing.popup - delay,
                iam_ease_out_cubic, iam_ease_in_cubic, animation.cut);
            if (amount <= invisible)
                continue;
            const auto offset = Axis(static_cast<Dir>(i)) * ((amount - 1) * 6 * s) +
                Point{shaking ? animation.shake * s : 0, 0};
            const auto rect = Translate(*mark_boxes_[i], offset);
            paint.opacity(amount);
            paint.shell(rect);
            const float accent = target({rect.min + Point{4, 4} * s, rect.max - Point{4, 4} * s}, choice, scope,
                selected ? TargetState::Selected : TargetState::Rest);
            const char index = static_cast<char>('1' + i);
            label(rect, {&index, 1}, 10 * s, Blend(color.ink_secondary, color.accent, accent), TextRole::Caption);
            label(rect, choice.label, 24 * s + artwork(choice, rect, 24 * s),
                choice.enabled ? color.ink : color.disabled);
        }
    }

    if (!popups_.empty()) {
        for (std::size_t level = first_popup; level < popups_.size(); ++level) {
            const auto& panel = popups_[level];
            const auto owner = motion.owner("dropdown", panel.id);
            const bool live = level < animation.active_popups;
            const float amount = Visibility(motion, owner, "reveal", live, timing.popup,
                iam_ease_out_cubic, iam_ease_in_cubic, animation.cut);
            if (amount <= invisible)
                continue;
            const bool clipped = !level && amount < 1;
            const Point slide{level ? (panel.left ? 4 : -4) * s * (1 - amount) : 0, 0};
            if (clipped) {
                auto clip = panel.rect;
                if (panel.above)
                    clip.min.y = clip.max.y - clip.size().y * amount;
                else
                    clip.max.y = clip.min.y + clip.size().y * amount;
                clip.min.x = settings_.work.min.x;
                clip.max.x = settings_.work.max.x;
                draw.PushClipRect(Vec(grid.snap(clip.min)), Vec(grid.snap(clip.max)), true);
            }
            paint.opacity(level ? amount : 1);
            paint.menu(Translate(panel.rect, slide));
            std::size_t item_index = 0;
            for (auto item : popup_items_) {
                if (item.level != level || item.rect.size().y <= 0)
                    continue;
                const bool hovered = live && animation.pointer_live && item.rect.contains(pointer);
                const bool invoked = !animation.invoked.empty() && animation.invoked == item.id;
                item.rect = Translate(item.rect, slide + Point{
                    !animation.shake_id.empty() && item.id == animation.shake_id ? animation.shake * s : 0, 0});
                if (item.kind == ControlKind::Separator) {
                    const float y = item.rect.min.y + item.rect.size().y * 0.5f;
                    paint.separator({item.rect.min.x + 10 * s, y}, {item.rect.max.x - 10 * s, y});
                    continue;
                }
                const bool header = item.kind == ControlKind::Header;
                if (!header)
                    target(item.rect, item, ImHashData(&item_index, sizeof(item_index), owner),
                        State(item.enabled, invoked, hovered, pressed));
                ++item_index;
                if (item.checked)
                    paint.check_mark({item.rect.min, {item.rect.min.x + 16 * s, item.rect.max.y}}, item.enabled);
                const float option_width = item.item && item.item->on_option ? 22 * s : 0;
                const float shortcut_width = item.item && !item.item->shortcut.empty()
                    ? type_.width(item.item->shortcut, TextRole::Caption) : 0;
                auto label_rect = item.rect;
                label_rect.max.x -= option_width + (shortcut_width > 0 ? shortcut_width + 16 * s : 0)
                    + (item.kind == ControlKind::Submenu ? 18 * s : 0);
                artwork(item, label_rect, 22 * s);
                label(label_rect, item.label, 22 * s + panel.icon_column,
                    header ? color.ink_secondary : item.enabled ? color.ink : color.disabled,
                    header ? TextRole::Section : TextRole::Value);
                if (item.kind == ControlKind::Submenu) {
                    const Rect chrome{{item.rect.max.x - 20 * s, item.rect.min.y}, item.rect.max};
                    type_.glyph(draw, grid, chrome, lucide::icon_chevron_right, nullptr, paint.ink(color.ink_secondary));
                }
                if (option_width > 0) {
                    const Point min{item.rect.max.x - 16 * s, item.rect.min.y + 6 * s};
                    paint.option_box({min, min + Point{10, 10} * s}, Painter::color(color.ring));
                }
                if (shortcut_width > 0) {
                    const float right = item.rect.max.x - option_width - 8 * s;
                    const Rect hint{{right - shortcut_width, item.rect.min.y}, {right + 1 / grid.density.x, item.rect.max.y}};
                    label(hint, item.item->shortcut, 0, color.ink_secondary, TextRole::Caption);
                }
            }
            if (clipped)
                draw.PopClipRect();
        }
    }
    draw.PopClipRect();
}

void OverlayView::toast(ImDrawList& draw, const Theme& theme, const AtlasAssets& atlas, PixelGrid grid,
    const Typography& type, Rect work, float scale, float rounding,
    Point pointer, const ItemDesc& command, float opacity) {
    const auto text = command.label.empty() ? std::string_view(command.alt_text) : std::string_view(command.label);
    const float inset = 10 * scale;
    const float icon_width = 22 * scale;
    const auto start = pointer + Point{14, 18} * scale;
    const float width = 2 * inset + icon_width + type.width(text);
    const auto box = ClampRect({start, start + Point{width, 32 * scale}}, work);
    Painter paint(draw, theme, atlas, grid, scale);
    paint.opacity(opacity);
    paint.rounding(rounding);
    draw.PushClipRect(Vec(work.min), Vec(work.max), true);
    paint.menu(box);
    const auto slot = IconSlot(box, inset, scale);
    if (AtlasAssets::has_icon(command.icon))
        paint.icon(type, slot, command.icon, true);
    else
        paint.monogram(type, slot, text, true);
    type.label(draw, grid, box, text, inset + icon_width, paint.ink(theme.colors.ink));
    draw.PopClipRect();
}

} // namespace hotbox::detail

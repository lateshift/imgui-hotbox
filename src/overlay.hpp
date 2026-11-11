#pragma once

#include "paint.hpp"
#include "motion.hpp"
#include "registry_data.hpp"

#include <hotbox/theme.hpp>
#include <span>

namespace hotbox::detail {

enum class ControlKind {
    Menu, Recent, Sets, Controls, Command, Submenu, Separator, Header,
    ChooseSet, Pin, Rows, Ticks, Overflow, Result,
};

struct Control {
    ControlKind kind;
    std::string_view id, label;
    Rect rect{};
    const ItemDesc* item = nullptr;
    bool enabled = true;
    bool checked = false;
    std::size_t level = 0;
    const Icon* menu_icon = nullptr;
    int row = 0;
    std::string_view detail{}; // The menu path of a search result.
};

/// A ranked search result or a recent command, kept by ID so it survives registry changes.
struct SearchRow {
    CommandId id;
    std::string path;
};

Control Resolve(const Registry& registry, const Layout& layout, std::string_view id, bool submenu = false);

struct ViewSettings {
    Point origin;
    Rect work;
    float scale;
    bool rows;
    bool ticks;
    bool pinned;
    bool monograms;
};

struct OverlayAnimation {
    Motion& motion;
    float rounding = 1;
    double rows_age = 0;
    double marking_age = 0;
    ImGuiID marking_scope = 0;
    Dir first_mark = Dir::N;
    std::size_t active_popups = 0;
    bool mark_live = false;
    bool pointer_live = false;
    bool cut = false;
    std::string_view invoked{};
    bool hold_scene = false;
    bool hold_invoked = false;
    std::string_view shake_id{};
    float shake = 0;
    bool search_live = false;
    std::size_t search_selected = static_cast<std::size_t>(-1);
    const Control* focus = nullptr; // Keyboard focus, drawn with a focus ring.
    bool focus_cut = false;          // The ring appears in place instead of travelling.
};

/// Text and placement of a hover or keyboard tooltip.
struct TooltipContent {
    std::string_view title, shortcut, body;
    bool warning = false; // The body explains why the command is disabled.
};
TooltipContent TooltipFor(const ItemDesc& item, bool enabled);

enum class OverlayLayer { All, Marking, Popups };

class OverlayView {
public:
    void reserve(const Layout& layout);
    void begin(const Layout& layout, ViewSettings settings, const Typography& type);
    void marking(Point pivot, const std::array<Control, 8>& choices);
    void popup(const Registry& registry, const Layout& layout, ControlKind kind,
        std::string_view id, Rect anchor, std::size_t level, std::span<const CommandId> overflow = {},
        ImGuiID scope = 0);
    /// Lay out the search field above the top row and its results below it.
    void search(const Registry& registry, const Layout& layout, std::string_view query,
        std::span<const SearchRow> rows, bool recent);

    const Control* title(Point pointer) const;
    const Control* popup_item(Point pointer) const;
    const Control* anchor(ControlKind kind, std::string_view id, std::size_t level) const;
    const Control* result(Point pointer) const;
    std::span<const Control> titles() const { return titles_; }
    std::span<const Control> popup_items() const { return popup_items_; }
    std::span<const Control> results() const { return results_; }
    std::optional<Rect> mark_box(Dir direction) const { return mark_boxes_[static_cast<unsigned>(direction)]; }
    bool searching() const { return search_shell_.has_value(); }
    std::optional<Dir> mark_at(Point pointer) const;
    Rect overflow_anchor(Point pivot) const;
    bool over_popup(Point point) const;
    bool on_chrome(Point point) const;

    void draw(ImDrawList& draw, const Theme& theme, const AtlasAssets& atlas, PixelGrid grid,
        const OverlayAnimation& animation, Point pointer, bool pressed, float scrim, bool scrim_visible,
        bool show_rows, const Control* active_title, const std::array<Control, 8>* choices, std::optional<Dir> armed,
        Point pivot, std::span<const Point> ink, OverlayLayer layer = OverlayLayer::All,
        std::size_t first_popup = 0) const;
    static void toast(ImDrawList& draw, const Theme& theme, const AtlasAssets& atlas, PixelGrid grid,
        const Typography& type, Rect work, float scale, float rounding,
        Point pointer, const ItemDesc& command, float opacity);
    static void tooltip(ImDrawList& draw, const Theme& theme, const AtlasAssets& atlas, PixelGrid grid,
        const Typography& type, Rect work, float scale, float rounding,
        Point anchor, std::optional<Rect> beside, const TooltipContent& content, float opacity);

private:
    struct Strip {
        std::size_t start, end;
        int row;
    };
    struct RowShape {
        Rect rect;
        int row;
    };
    struct Panel {
        Rect rect;
        ImGuiID id;
        bool above;
        bool left;
        float icon_column; // Shared by every row so labels stay aligned when only some rows have artwork.
    };
    void layout_strip(Strip strip, float center_y);
    void layout_center(float center_y);

    ViewSettings settings_{};
    Typography type_;
    std::vector<Control> titles_, popup_items_, results_;
    std::vector<Strip> strips_;
    std::vector<RowShape> shells_;
    std::vector<Panel> popups_;
    std::vector<std::pair<Point, int>> dividers_;
    Boxes mark_boxes_;
    std::size_t center_start_ = 0;
    float ticks_top_ = 0, ticks_bottom_ = 0;
    std::optional<Rect> search_shell_, search_panel_;
    std::string_view query_;
    bool recent_results_ = false;
    float results_icon_column_ = 0;
};

} // namespace hotbox::detail

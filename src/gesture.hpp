#pragma once

#include "geometry.hpp"

#include <cstddef>
#include <span>

namespace hotbox::detail {

struct Corner {
    Dir incoming;
    Point pivot;
};

class Stroke {
public:
    void reset(Point start);
    void add(Point point);
    std::span<const Point> points() const { return {points_.data(), size_}; }
    std::optional<Dir> dominant(float dead_zone) const;
    std::optional<Corner> corner() const;

private:
    std::array<Point, 128> points_{};
    std::size_t size_ = 0;
};

struct GestureTiming {
    float dead_zone;
    float reveal_delay_ms;
    float markahead_window_ms;
    float submenu_dwell_ms;
};

enum class MarkAction { None, Select, Cancel, Branch };
struct MarkResult {
    MarkAction action = MarkAction::None;
    Dir dir = Dir::N;
    Point pivot;
    bool ahead = false;
};

/// One marking stroke, including sticky click and up to three nested levels.
class MarkGesture {
public:
    void begin(Point pivot);
    MarkResult step(float dt_ms, Point pointer, bool released, bool pressed,
        std::optional<Dir> hit_box, const std::array<bool, 8>& submenus, GestureTiming timing);
    void branch(Point pivot, Point pointer, bool sticky = false);
    bool can_branch() const { return depth_ < 3; }
    Point pivot() const { return pivot_; }
    std::optional<Dir> armed() const { return armed_; }
    bool visible() const { return visible_; }
    bool ahead() const { return ahead_; }
    bool sticky() const { return sticky_; }
    std::span<const Point> ink() const { return ink_.points(); }

private:
    Stroke segment_, ink_;
    Point pivot_;
    std::optional<Dir> armed_;
    float age_ = 0;
    float dwell_ = 0;
    bool visible_ = false;
    bool ahead_ = false;
    bool sticky_ = false;
    unsigned depth_ = 0;
};

} // namespace hotbox::detail

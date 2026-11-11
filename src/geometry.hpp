#pragma once

#include <hotbox/types.hpp>

#include <array>
#include <optional>

namespace hotbox::detail {

struct Point {
    float x = 0;
    float y = 0;
    friend bool operator==(Point, Point) = default;
};
constexpr Point operator+(Point a, Point b) { return {a.x + b.x, a.y + b.y}; }
constexpr Point operator-(Point a, Point b) { return {a.x - b.x, a.y - b.y}; }
constexpr Point operator*(Point point, float scale) { return {point.x * scale, point.y * scale}; }
constexpr float Dot(Point a, Point b) { return a.x * b.x + a.y * b.y; }

struct Rect {
    Point min, max;
    constexpr Point size() const { return max - min; }
    constexpr bool contains(Point p) const {
        return p.x >= min.x && p.x < max.x && p.y >= min.y && p.y < max.y;
    }
};

using Boxes = std::array<std::optional<Rect>, 8>;

float Length(Point point);
Zone ClassifyZone(Point delta, float center_radius);
Point Axis(Dir direction);
std::optional<Dir> Sector(Point delta, float dead_zone, std::optional<Dir> previous = {});
Rect ClampRect(Rect box, Rect work);
Boxes LayoutBoxes(Point pivot, const std::array<float, 8>& widths, Rect work,
    float height, float radius, float gap);

} // namespace hotbox::detail

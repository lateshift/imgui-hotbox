#include "geometry.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace hotbox::detail {
namespace {

constexpr float diagonal = 0.70710678f;
constexpr std::array<Point, 8> axes{{
    {0, -1}, {diagonal, -diagonal}, {1, 0}, {diagonal, diagonal},
    {0, 1}, {-diagonal, diagonal}, {-1, 0}, {-diagonal, -diagonal},
}};
constexpr float pi = std::numbers::pi_v<float>;
constexpr float sector_angle = pi / 4;
constexpr float hysteresis = 4 * pi / 180;

bool Fits(Rect candidate, const Boxes& boxes, float gap) {
    return std::ranges::none_of(boxes, [&](const auto& box) {
        return box && candidate.min.x < box->max.x + gap && candidate.max.x + gap > box->min.x &&
               candidate.min.y < box->max.y + gap && candidate.max.y + gap > box->min.y;
    });
}

std::optional<Rect> Place(Rect preferred, Point axis, const Boxes& boxes, Rect work, float gap) {
    const auto size = preferred.size();
    const auto desired = ClampRect(preferred, work);
    if (Fits(desired, boxes, gap))
        return desired;

    auto last = desired.min;
    for (float offset = 4; offset <= 128; offset += 4) {
        const auto translation = axis * offset;
        const auto candidate = ClampRect({preferred.min + translation, preferred.max + translation}, work);
        if (candidate.min == last)
            continue;
        if (Fits(candidate, boxes, gap))
            return candidate;
        last = candidate.min;
    }

    // A clamped pivot may stack multiple anchors against the same edge. Search
    // edges and occupied-box boundaries for the nearest remaining placement.
    std::array<float, 19> xs{desired.min.x, work.min.x, work.max.x - size.x};
    std::array<float, 19> ys{desired.min.y, work.min.y, work.max.y - size.y};
    std::size_t count = 3;
    for (const auto& box : boxes) {
        if (!box)
            continue;
        xs[count] = box->min.x - gap - size.x;
        ys[count++] = box->min.y - gap - size.y;
        xs[count] = box->max.x + gap;
        ys[count++] = box->max.y + gap;
    }
    std::optional<Rect> best;
    float best_distance = std::numeric_limits<float>::infinity();
    for (std::size_t x = 0; x < count; ++x) {
        for (std::size_t y = 0; y < count; ++y) {
            const Point start{xs[x], ys[y]};
            const auto candidate = ClampRect({start, start + size}, work);
            const auto delta = candidate.min - desired.min;
            const float distance = Dot(delta, delta);
            if (distance < best_distance && Fits(candidate, boxes, gap)) {
                best = candidate;
                best_distance = distance;
            }
        }
    }
    return best;
}

} // namespace

float Length(Point point) { return std::hypot(point.x, point.y); }

Zone ClassifyZone(Point delta, float center_radius) {
    if (Dot(delta, delta) <= center_radius * center_radius)
        return Zone::Center;
    if (std::abs(delta.y) >= std::abs(delta.x))
        return delta.y < 0 ? Zone::North : Zone::South;
    return delta.x < 0 ? Zone::West : Zone::East;
}

Point Axis(Dir direction) { return axes[static_cast<unsigned>(direction)]; }

std::optional<Dir> Sector(Point delta, float dead_zone, std::optional<Dir> previous) {
    if (Dot(delta, delta) <= dead_zone * dead_zone)
        return {};
    const float angle = std::atan2(delta.x, -delta.y);
    if (previous) {
        const float center = static_cast<float>(*previous) * sector_angle;
        if (std::abs(std::remainder(angle - center, 2 * pi)) <= sector_angle / 2 + hysteresis)
            return previous;
    }
    const int nearest = static_cast<int>(std::floor(angle / sector_angle + 0.5f));
    return static_cast<Dir>((nearest + 8) % 8);
}

Rect ClampRect(Rect box, Rect work) {
    const auto size = box.size();
    const Point clamped_size{std::clamp(size.x, 0.0f, std::max(work.size().x, 0.0f)),
                             std::clamp(size.y, 0.0f, std::max(work.size().y, 0.0f))};
    const Point min{std::clamp(box.min.x, work.min.x, std::max(work.min.x, work.max.x - clamped_size.x)),
                    std::clamp(box.min.y, work.min.y, std::max(work.min.y, work.max.y - clamped_size.y))};
    return {min, min + clamped_size};
}

Boxes LayoutBoxes(Point pivot, const std::array<float, 8>& widths, Rect work,
    float height, float radius, float gap) {
    Boxes boxes;
    if (work.size().x <= 0 || work.size().y <= 0)
        return boxes;
    for (const auto dir : {Dir::N, Dir::E, Dir::S, Dir::W, Dir::NE, Dir::SE, Dir::SW, Dir::NW}) {
        const auto index = static_cast<unsigned>(dir);
        const float width = std::min(widths[index], work.size().x);
        if (width <= 0)
            continue;
        const auto axis = Axis(dir);
        const auto anchor = pivot + axis * radius;
        const Point min{anchor.x - (axis.x < 0 ? width : axis.x > 0 ? 0 : width / 2),
                        anchor.y - height / 2};
        boxes[index] = Place({min, min + Point{width, std::min(height, work.size().y)}},
            axis, boxes, work, gap);
    }
    return boxes;
}

} // namespace hotbox::detail

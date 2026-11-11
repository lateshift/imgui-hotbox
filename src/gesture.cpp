#include "gesture.hpp"

#include <algorithm>
#include <cmath>

namespace hotbox::detail {
namespace {

constexpr float turn_cosine = 0.64278761f;
constexpr float branch_travel = 40;
constexpr float branch_exit = 12;
constexpr float click_threshold_ms = 200;

bool Spike(Point previous, Point sample, Point next) {
    const float before = Length(sample - previous);
    const float after = Length(next - sample);
    return std::min(before, after) > 8 &&
           Length(next - previous) < std::min(before, after) * 0.45f;
}

} // namespace

void Stroke::reset(Point start) {
    points_[0] = start;
    size_ = 1;
}

void Stroke::add(Point point) {
    if (!size_) {
        reset(point);
        return;
    }
    const auto delta = point - points_[size_ - 1];
    if (Dot(delta, delta) < 0.75f * 0.75f)
        return;
    if (size_ == points_.size()) {
        for (std::size_t i = 1; i < size_ / 2; ++i)
            points_[i] = points_[i * 2];
        points_[size_ / 2] = points_[size_ - 1];
        size_ = size_ / 2 + 1;
    }
    points_[size_++] = point;
}

std::optional<Dir> Stroke::dominant(float dead_zone) const {
    if (size_ < 2)
        return {};
    Point heading{};
    float furthest = 0;
    for (std::size_t i = 1; i < size_; ++i) {
        const auto delta = points_[i] - points_[0];
        const float distance = Length(delta);
        if (distance <= furthest || distance <= dead_zone)
            continue;
        if (i + 1 < size_ && Spike(points_[i - 1], points_[i], points_[i + 1]))
            continue;
        // Limit the influence of one large sample and ignore retraction/jitter.
        heading = heading + delta * (std::min(distance - furthest, 20.0f) / distance);
        furthest = distance;
    }
    return Sector(heading, 0);
}

std::optional<Corner> Stroke::corner() const {
    if (size_ < 3)
        return {};
    const auto end = points_[size_ - 1];
    float best = 0;
    std::optional<Corner> result;
    for (std::size_t i = 1; i + 1 < size_; ++i) {
        const auto sample = points_[i];
        const auto first = sample - points_[0];
        const auto second = end - sample;
        const float travel = Length(first);
        const float exit = Length(second);
        if (travel < branch_travel || exit < branch_exit ||
            Spike(points_[i - 1], sample, points_[i + 1]))
            continue;
        const float alignment = Dot(first, second) / (travel * exit);
        if (alignment >= turn_cosine || alignment < -0.85f)
            continue;
        const float score = (1 - alignment) * std::min(exit, travel);
        if (score > best) {
            result = Corner{*Sector(first, 0), sample};
            best = score;
        }
    }
    return result;
}

void MarkGesture::begin(Point pivot) {
    pivot_ = pivot;
    segment_.reset(pivot);
    ink_.reset(pivot);
    armed_.reset();
    age_ = dwell_ = 0;
    visible_ = ahead_ = sticky_ = false;
    depth_ = 0;
}

MarkResult MarkGesture::step(float dt_ms, Point pointer, bool released, bool pressed,
    std::optional<Dir> hit_box, const std::array<bool, 8>& submenus, GestureTiming timing) {
    age_ += dt_ms;
    const auto index = [](Dir dir) { return static_cast<unsigned>(dir); };
    const auto angle = Sector(pointer - pivot_, timing.dead_zone, armed_);
    if (sticky_) {
        armed_ = hit_box ? hit_box : angle;
        if (!pressed)
            return {};
        if (!armed_)
            return {MarkAction::Cancel, Dir::N, {}, false};
        if (can_branch() && submenus[index(*armed_)])
            return {MarkAction::Branch, *armed_, pointer, false};
        return {MarkAction::Select, *armed_, {}, false};
    }

    segment_.add(pointer);
    ink_.add(pointer);
    if (!visible_ && angle && age_ < timing.reveal_delay_ms)
        ahead_ = true;
    if (ahead_ && age_ > timing.markahead_window_ms)
        ahead_ = false;
    if (!ahead_ && age_ >= timing.reveal_delay_ms)
        visible_ = true;

    const auto next = angle && ahead_ ? segment_.dominant(timing.dead_zone) : angle;
    dwell_ = next && armed_ == next ? dwell_ + dt_ms : 0;
    armed_ = next;

    if (can_branch() && angle && std::ranges::any_of(submenus, [](bool submenu) { return submenu; })) {
        if (const auto bend = segment_.corner(); bend && submenus[index(bend->incoming)])
            return {MarkAction::Branch, bend->incoming, bend->pivot, ahead_};
        if (armed_ && dwell_ >= timing.submenu_dwell_ms && submenus[index(*armed_)] && !released)
            return {MarkAction::Branch, *armed_, pointer, ahead_};
    }

    if (!released)
        return {};
    if (!angle && age_ <= click_threshold_ms && !ahead_) {
        visible_ = sticky_ = true;
        armed_.reset();
        return {};
    }
    if (armed_)
        return {MarkAction::Select, *armed_, {}, ahead_};
    return {MarkAction::Cancel, Dir::N, {}, false};
}

void MarkGesture::branch(Point pivot, Point pointer, bool sticky) {
    pivot_ = pivot;
    segment_.reset(pivot);
    segment_.add(pointer);
    armed_.reset();
    dwell_ = 0;
    sticky_ |= sticky;
    ++depth_;
}

} // namespace hotbox::detail

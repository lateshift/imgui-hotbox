#pragma once

#include "geometry.hpp"

namespace hotbox::detail {

enum class Phase { Closed, Arming, Open };
enum class Transition { None, Opened, Closed, Tapped };

struct TriggerInput {
    float dt_ms;
    Point pointer;
    bool pointer_valid;
    bool pressed;
    bool held;
    bool cancel;
    bool toggle_pin;
};

class TriggerState {
public:
    Transition step(TriggerInput input, float hold_ms);
    Transition open(Point point);
    Transition close();
    Phase phase() const { return phase_; }
    Point origin() const { return origin_; }
    bool pinned() const { return pinned_; }
    void set_pinned(bool value) { pinned_ = value; }
    float latency_ms() const { return age_; }

private:
    Phase phase_ = Phase::Closed;
    Point origin_;
    float age_ = 0;
    bool pinned_ = false;
    bool chord_open_ = false;
};

} // namespace hotbox::detail

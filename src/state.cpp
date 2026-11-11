#include "state.hpp"

namespace hotbox::detail {

Transition TriggerState::step(TriggerInput input, float hold_ms) {
    if (input.cancel)
        return close();

    if (phase_ == Phase::Closed) {
        if (!input.pressed || !input.pointer_valid)
            return Transition::None;
        phase_ = Phase::Arming;
        origin_ = input.pointer;
        age_ = 0;
        chord_open_ = true;
        if (hold_ms > 0)
            return Transition::None;
    } else {
        age_ += input.dt_ms;
    }

    if (phase_ == Phase::Arming) {
        const auto delta = input.pointer - origin_;
        const bool moved = input.pointer_valid && Dot(delta, delta) >= 1;
        if (!input.held) {
            close();
            return !moved && age_ < hold_ms ? Transition::Tapped : Transition::None;
        }
        if (moved || age_ >= hold_ms) {
            phase_ = Phase::Open;
            return Transition::Opened;
        }
        return Transition::None;
    }

    if (input.toggle_pin)
        pinned_ = !pinned_;
    if (chord_open_ && !input.held && !pinned_)
        return close();
    return Transition::None;
}

Transition TriggerState::open(Point point) {
    const bool was_open = phase_ == Phase::Open;
    phase_ = Phase::Open;
    origin_ = point;
    chord_open_ = false;
    age_ = 0;
    return was_open ? Transition::None : Transition::Opened;
}

Transition TriggerState::close() {
    const bool was_open = phase_ == Phase::Open;
    phase_ = Phase::Closed;
    chord_open_ = false;
    pinned_ = false;
    return was_open ? Transition::Closed : Transition::None;
}

} // namespace hotbox::detail

#pragma once

#include <hotbox/hotbox.hpp>
#include <im_anim.h>

namespace hotbox::detail {

bool SystemReduceMotion();

class Motion {
public:
    Motion();

    void begin();
    void set(MotionPrefs prefs);
    MotionPrefs prefs() const { return prefs_; }
    bool enabled() const { return prefs_.speed > 0 && !prefs_.reduced_motion; }
    bool animating() const { return animating_; }
    double time() const { return time_; }
    void keep_rendering() { animating_ = true; }
    ImGuiID owner(std::string_view name, ImGuiID scope = 0) const;

    float tween(ImGuiID owner, const char* channel, float target, float seconds, int ease,
        float initial = 0, bool cut = false);
    ImVec4 color(ImGuiID owner, const char* channel, ImVec4 target, float seconds, int ease,
        ImVec4 initial);
    Theme palette(const Theme& target, float& rounding);

private:
    MotionPrefs prefs_;
    ImGuiID seed_;
    bool animating_ = false;
    bool palette_initialized_ = false;
    double time_ = 0;
};

} // namespace hotbox::detail

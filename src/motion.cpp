#include "motion.hpp"

#include <imgui_internal.h>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cmath>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace hotbox {
namespace {

thread_local ImGuiContext* frame_context = nullptr;
thread_local int motion_frame = -1;

void ForgetContext(ImGuiContext* context, ImGuiContextHook*) {
    if (frame_context == context) {
        frame_context = nullptr;
        motion_frame = -1;
    }
}

} // namespace

void begin_frame(bool imanim_already_updated) {
    auto* context = ImGui::GetCurrentContext();
    assert(context && context->WithinFrameScope);
    if (frame_context == context && motion_frame == ImGui::GetFrameCount())
        return;
    if (frame_context != context) {
        if (std::ranges::none_of(context->Hooks, [](const auto& hook) { return hook.Callback == ForgetContext; })) {
            ImGuiContextHook hook;
            hook.Type = ImGuiContextHookType_Shutdown;
            hook.Callback = ForgetContext;
            ImGui::AddContextHook(context, &hook);
        }
        static const bool reserved = [] {
            iam_reserve(512, 32, 32, 16, 512);
            return true;
        }();
        (void)reserved;
    }
    frame_context = context;
    motion_frame = ImGui::GetFrameCount();
    if (!imanim_already_updated) {
        iam_update_begin_frame();
        iam_clip_update(ImGui::GetIO().DeltaTime);
    }
    if (motion_frame % 120 == 0) {
        iam_gc();
        iam_clip_gc();
    }
}

namespace detail {

#ifndef __APPLE__
bool SystemReduceMotion() {
#ifdef _WIN32
    BOOL animate = TRUE;
    return SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animate, 0) && !animate;
#else
    return false;
#endif
}
#endif

Motion::Motion() {
    static std::atomic<ImGuiID> next{1};
    seed_ = next.fetch_add(1, std::memory_order_relaxed);
}

void Motion::begin() {
    assert(frame_context == ImGui::GetCurrentContext() && motion_frame == ImGui::GetFrameCount() &&
        "Call hotbox::begin_frame() immediately after ImGui::NewFrame()");
    animating_ = false;
    time_ += enabled() ? ImGui::GetIO().DeltaTime * prefs_.speed : 0;
}

void Motion::set(MotionPrefs prefs) {
    if (!std::isfinite(prefs.speed) || prefs.speed < 0)
        prefs.speed = 0;
    prefs_ = prefs;
}

ImGuiID Motion::owner(std::string_view name, ImGuiID scope) const {
    const ImGuiID base = ImHashData(&scope, sizeof(scope), seed_);
    return name.empty() ? base : ImHashData(name.data(), name.size(), base);
}

float Motion::tween(ImGuiID id, const char* channel, float target, float seconds, int ease,
    float initial, bool cut) {
    const bool play = enabled() && seconds > 0 && !cut;
    const auto value = iam_tween_float(id, ImHashStr(channel), target,
        play ? seconds / prefs_.speed : 0, iam_ease_preset(ease),
        play ? iam_policy_crossfade : iam_policy_cut, ImGui::GetIO().DeltaTime, initial);
    animating_ |= play && std::abs(value - target) > 0.0001f;
    return value;
}

ImVec4 Motion::color(ImGuiID id, const char* channel, ImVec4 target, float seconds, int ease, ImVec4 initial) {
    const bool play = enabled() && seconds > 0;
    const auto value = iam_tween_color(id, ImHashStr(channel), target,
        play ? seconds / prefs_.speed : 0, iam_ease_preset(ease),
        play ? iam_policy_crossfade : iam_policy_cut, iam_col_oklab, ImGui::GetIO().DeltaTime, initial);
    constexpr float epsilon = 0.0001f;
    animating_ |= play && (std::abs(value.x - target.x) > epsilon || std::abs(value.y - target.y) > epsilon ||
        std::abs(value.z - target.z) > epsilon || std::abs(value.w - target.w) > epsilon);
    return value;
}

Theme Motion::palette(const Theme& target, float& rounding) {
    const auto id = owner("theme");
    const float shape = target.shape == ShapeSetting::Rounded ? 1.0f : 0.0f;
    const float duration = palette_initialized_ ? target.motion.section : 0;
    rounding = tween(id, "rounding", shape, duration, iam_ease_in_out_quad, 1 - shape);
    auto theme = target;
    const auto blend = [&](const char* channel, ImVec4& value) {
        auto initial = value;
        initial.w = 1 - value.w;
        if (initial.w == value.w)
            initial.w = 0;
        value = color(id, channel, value, duration, iam_ease_in_out_quad, initial);
    };
    auto& c = theme.colors;
    blend("scrim", c.scrim);
    blend("surface", c.surface);
    blend("edge", c.edge);
    blend("shadow", c.shadow);
    blend("pill", c.pill);
    blend("pill hover", c.pill_hover);
    blend("pill pressed", c.pill_pressed);
    blend("pill ink", c.pill_ink);
    blend("pill shadow", c.pill_shadow);
    blend("sector", c.sector);
    blend("sector edge", c.sector_edge);
    blend("trail", c.trail);
    blend("ring", c.ring);
    blend("ink", c.ink);
    blend("ink secondary", c.ink_secondary);
    blend("disabled", c.disabled);
    blend("accent", c.accent);
    blend("accent ink", c.accent_ink);
    blend("focus", c.focus);
    blend("key fill", c.key_fill);
    blend("key edge", c.key_edge);
    blend("key ink", c.key_ink);
    palette_initialized_ = true;
    return theme;
}

} // namespace detail
} // namespace hotbox

#pragma once

#include <hotbox/hotbox.hpp>

#include <string_view>

namespace harness {

/// Scripted input for one frame of the README tour.
struct TourInput {
    ImVec2 pointer;
    bool left = false;    // Left mouse button held.
    bool alt = false;     // ⌥ / Alt held.
    bool space = false;   // Space held.
    std::string_view caption;
    float caption_age = 0; // Seconds since the caption appeared.
};

/// Frame rate of the tour. The harness advances ImGui by 1 / tour_fps per frame.
inline constexpr float tour_fps = 30;

/// Number of frames in the whole tour.
int TourFrameCount();

/// Input at a time in seconds from the start of the tour.
TourInput TourAt(float seconds);

/// Queue the ImGui input events that move from `previous` to `current`.
void FeedTour(const TourInput& previous, const TourInput& current, ImGuiIO& io);

/// Draw the pointer, the held keys and the caption above the hotbox.
void DrawTourOverlay(ImDrawList& draw, const hotbox::Theme& theme, const hotbox::fonts::Faces& faces,
    const TourInput& input, ImVec2 display);

} // namespace harness

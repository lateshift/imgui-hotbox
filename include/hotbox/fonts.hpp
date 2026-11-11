#pragma once

#include <filesystem>

struct ImFont;
struct ImFontAtlas;

namespace hotbox::fonts {

struct Faces {
    ImFont* regular = nullptr;
    ImFont* medium = nullptr;
    ImFont* semibold = nullptr;
    ImFont* lucide = nullptr;

    explicit operator bool() const { return regular && medium && semibold && lucide; }
};

/// Convert a design em size using the primary source of the loaded font.
float imgui_size(const ImFont* font, float em_size);

/// Populate an atlas before its first frame. Pointers belong to the atlas.
Faces load_embedded(ImFontAtlas& atlas);

/// Read the Inter and Lucide snapshots from the given vendor directory.
Faces load_from_files(ImFontAtlas& atlas, const std::filesystem::path& vendor_directory);

} // namespace hotbox::fonts

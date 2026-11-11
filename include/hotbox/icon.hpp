#pragma once

#include <imgui.h>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace hotbox {

namespace detail { class AtlasAssets; class Painter; class Typography; }

/// An icon source. Atlas upload and painting are handled separately from registration.
struct Icon {
    static Icon none();
    static Icon glyph(ImWchar codepoint);
    static Icon glyph(ImWchar codepoint, ImFont* font);
    static Icon svg(std::string_view source);
    static Icon svg_file(std::filesystem::path path);
    static Icon svg_pair(std::string_view regular_32, std::string_view compact_16);
    static Icon texture(ImTextureRef tex, ImVec2 size_pt,
        ImVec2 uv0 = {0, 0}, ImVec2 uv1 = {1, 1});
    static Icon monogram(std::string_view letters);

    Icon& tintable(bool value);
    Icon& color(ImU32 value);

private:
    friend class detail::AtlasAssets;
    friend class detail::Painter;
    friend class detail::Typography;

    struct VectorSource {
        std::string regular, compact;
    };
    enum class Kind { None, Glyph, Svg, Texture, Monogram };
    Kind kind_ = Kind::None;
    ImWchar codepoint_ = 0;
    ImFont* font_ = nullptr;
    std::string text_;
    std::shared_ptr<const VectorSource> vector_;
    ImTextureRef texture_;
    ImVec2 size_, uv0_, uv1_;
    bool tintable_ = true;
    ImU32 color_ = 0;
    bool has_color_ = false;
};

} // namespace hotbox

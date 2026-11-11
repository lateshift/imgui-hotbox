#include <hotbox/icon.hpp>

#include <fstream>
#include <iterator>
#include <utility>

namespace hotbox {

Icon Icon::none() { return {}; }
Icon Icon::glyph(ImWchar codepoint) { return glyph(codepoint, nullptr); }
Icon Icon::glyph(ImWchar codepoint, ImFont* font) {
    Icon icon;
    icon.kind_ = Kind::Glyph;
    icon.codepoint_ = codepoint;
    icon.font_ = font;
    return icon;
}
Icon Icon::svg(std::string_view source) { return svg_pair(source, {}); }

Icon Icon::svg_file(std::filesystem::path path) {
    std::ifstream file(path, std::ios::binary);
    const std::string source{std::istreambuf_iterator<char>(file), {}};
    return svg(source);
}

Icon Icon::svg_pair(std::string_view regular_32, std::string_view compact_16) {
    Icon icon;
    icon.kind_ = Kind::Svg;
    icon.vector_ = std::make_shared<VectorSource>(
        VectorSource{std::string(regular_32), std::string(compact_16)});
    return icon;
}
Icon Icon::texture(ImTextureRef tex, ImVec2 size_pt, ImVec2 uv0, ImVec2 uv1) {
    Icon icon;
    icon.kind_ = Kind::Texture;
    icon.texture_ = tex;
    icon.size_ = size_pt;
    icon.uv0_ = uv0;
    icon.uv1_ = uv1;
    return icon;
}
Icon Icon::monogram(std::string_view letters) {
    Icon icon;
    icon.kind_ = Kind::Monogram;
    icon.text_ = letters;
    return icon;
}
Icon& Icon::tintable(bool value) {
    tintable_ = value;
    return *this;
}
Icon& Icon::color(ImU32 value) {
    color_ = value;
    has_color_ = true;
    return *this;
}

} // namespace hotbox

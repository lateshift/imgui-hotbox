#pragma once

#include "registry_data.hpp"

#include <hotbox/theme.hpp>

#include <unordered_map>

namespace hotbox::detail {

struct ShadowMask {
    int extent = 0;
    int margin = 0;
    int edge = 0;
    std::vector<unsigned char> alpha;
};

ShadowMask MakeShadowMask(int radius, int blur);

struct ShadowSlice {
    ImFontAtlasRectId rect = ImFontAtlasRectId_Invalid;
    int margin = 0;
    int edge = 0;
};

struct AtlasImage {
    ImTextureRef texture;
    ImVec2 uv0, uv1;
    explicit operator bool() const { return texture._TexData || texture._TexID != ImTextureID_Invalid; }
};

/// Owns custom rectangle IDs, never texture handles or cached UV coordinates.
class AtlasAssets {
public:
    bool prepare(ImFontAtlas& atlas, const RegistrySnapshot& snapshot, float scale,
        float density, bool dynamic);
    void prepare_feedback(const Icon& icon);
    void release(ImFontAtlas* current);
    void forget();

    AtlasImage image(ImFontAtlasRectId id) const;
    AtlasImage svg(const Icon& icon, bool enabled) const;
    static bool has_icon(const Icon& icon);
    const ShadowSlice& shell(bool rounded) const { return shells_[rounded]; }
    const ShadowSlice& menu(bool rounded) const { return menus_[rounded]; }

private:
    struct SvgRect {
        ImFontAtlasRectId normal = ImFontAtlasRectId_Invalid;
        ImFontAtlasRectId muted = ImFontAtlasRectId_Invalid;
    };

    void release_svg();
    ShadowSlice shadow(int radius, int blur);
    void prepare_svg(const Icon& icon, float logical_size, int pixels);
    ImFontAtlasRectId pack(int width, int height, const unsigned char* pixels, bool rgba);

    ImFontAtlas* atlas_ = nullptr;
    const void* builder_ = nullptr;
    std::uint64_t revision_ = 0;
    float scale_ = 0, density_ = 0;
    bool dynamic_ = false;
    std::array<ShadowSlice, 2> shells_, menus_;
    std::unordered_map<const void*, std::array<SvgRect, 2>> svg_;
};

} // namespace hotbox::detail

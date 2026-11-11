#include "atlas.hpp"
#include "log.hpp"

#include <lunasvg.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>

#ifdef HOTBOX_WITH_SPDLOG
#include <spdlog/spdlog.h>
#endif

namespace hotbox::detail {
namespace {

std::size_t Pixel(int x, int y, int stride) {
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(stride) + static_cast<std::size_t>(x);
}

int SpritePixels(float size) {
    return static_cast<int>(std::lround(std::clamp(size, 1.0f, 512.0f)));
}

std::vector<unsigned char> RasterSvg(std::string_view source, int size) {
    if (source.empty())
        return {};
    auto doc = lunasvg::Document::loadFromData(source.data(), source.size());
    if (!doc || !std::isfinite(doc->width()) || !std::isfinite(doc->height()) ||
        doc->width() <= 0 || doc->height() <= 0)
        return {};
    lunasvg::Bitmap bitmap(size, size);
    if (bitmap.isNull())
        return {};
    bitmap.clear(0);
    const float edge = static_cast<float>(size);
    const float scale = std::min(edge / doc->width(), edge / doc->height());
    doc->render(bitmap, lunasvg::Matrix(scale, 0, 0, scale,
        (edge - doc->width() * scale) * 0.5f, (edge - doc->height() * scale) * 0.5f));
    bitmap.convertToRGBA();
    std::vector<unsigned char> rgba(Pixel(0, size, size) * 4);
    for (int row = 0; row < size; ++row) {
        std::memcpy(rgba.data() + Pixel(0, row, size) * 4,
            bitmap.data() + Pixel(0, row, bitmap.stride()), static_cast<std::size_t>(size) * 4);
    }
    return rgba;
}

} // namespace

ShadowMask MakeShadowMask(int radius, int blur) {
    // The central 3 texels supply a stretchable straight edge. Each edge tile
    // includes the complete rounded corner and the Gaussian's support.
    const int margin = blur + 1;
    const int edge = margin + radius + blur;
    const int extent = 2 * edge + 3;
    const auto count = Pixel(0, extent, extent);
    std::vector<double> shape(count), horizontal(count);
    const double half = static_cast<double>(extent) * 0.5 - margin - radius;
    for (int y = 0; y < extent; ++y) {
        for (int x = 0; x < extent; ++x) {
            const double dx = std::abs(x + 0.5 - extent * 0.5) - half;
            const double dy = std::abs(y + 0.5 - extent * 0.5) - half;
            const double distance = std::hypot(std::max(0.0, dx), std::max(0.0, dy)) +
                std::min(0.0, std::max(dx, dy)) - radius;
            shape[Pixel(x, y, extent)] = std::clamp(0.5 - distance, 0.0, 1.0);
        }
    }
    const double sigma = std::max(0.5, blur / 3.0);
    std::vector<double> kernel(static_cast<std::size_t>(2 * blur + 1));
    for (int x = -blur; x <= blur; ++x)
        kernel[static_cast<std::size_t>(x + blur)] = std::exp(-0.5 * (x / sigma) * (x / sigma));
    const auto norm = std::accumulate(kernel.begin(), kernel.end(), 0.0);
    for (auto& weight : kernel)
        weight /= norm;

    for (int y = 0; y < extent; ++y) {
        for (int x = 0; x < extent; ++x) {
            double value = 0;
            for (int k = std::max(-blur, -x); k <= std::min(blur, extent - 1 - x); ++k)
                value += shape[Pixel(x + k, y, extent)] * kernel[static_cast<std::size_t>(k + blur)];
            horizontal[Pixel(x, y, extent)] = value;
        }
    }

    ShadowMask mask{extent, margin, edge, std::vector<unsigned char>(count)};
    for (int y = 1; y + 1 < extent; ++y) {
        for (int x = 1; x + 1 < extent; ++x) {
            double value = 0;
            for (int k = std::max(-blur, -y); k <= std::min(blur, extent - 1 - y); ++k)
                value += horizontal[Pixel(x, y + k, extent)] * kernel[static_cast<std::size_t>(k + blur)];
            mask.alpha[Pixel(x, y, extent)] =
                static_cast<unsigned char>(std::lround(std::clamp(value, 0.0, 1.0) * 255));
        }
    }
    return mask;
}

void AtlasAssets::forget() {
    atlas_ = nullptr;
    builder_ = nullptr;
    dynamic_ = false;
    shells_ = {};
    menus_ = {};
    svg_.clear();
}

void AtlasAssets::release(ImFontAtlas* current) {
    if (atlas_ && atlas_ == current && builder_ == atlas_->Builder && atlas_->Builder) {
        const auto remove = [&](ImFontAtlasRectId id) {
            if (id != ImFontAtlasRectId_Invalid)
                atlas_->RemoveCustomRect(id);
        };
        for (const auto& slice : shells_)
            remove(slice.rect);
        for (const auto& slice : menus_)
            remove(slice.rect);
        release_svg();
    }
    forget();
}

void AtlasAssets::release_svg() {
    for (const auto& [key, variants] : svg_) {
        for (const auto& rect : variants) {
            if (rect.normal != ImFontAtlasRectId_Invalid)
                atlas_->RemoveCustomRect(rect.normal);
            if (rect.muted != ImFontAtlasRectId_Invalid)
                atlas_->RemoveCustomRect(rect.muted);
        }
    }
    svg_.clear();
}

bool AtlasAssets::prepare(ImFontAtlas& atlas, const RegistrySnapshot& snapshot, float scale,
    float density, bool dynamic) {
    const bool same_grid = atlas_ == &atlas && builder_ == atlas.Builder && scale_ == scale &&
        density_ == density && dynamic_ == dynamic;
    if (same_grid && revision_ == snapshot.revision)
        return false;
    if (same_grid)
        release_svg();
    else
        release(&atlas);
    atlas_ = &atlas;
    builder_ = atlas.Builder;
    scale_ = scale;
    density_ = density;
    revision_ = snapshot.revision;
    dynamic_ = dynamic;
    if (!dynamic)
        return false;

    const float pixels_per_pt = scale * density;
    if (!same_grid) {
        const int shell_blur = SpritePixels(8 * pixels_per_pt);
        const int menu_blur = SpritePixels(20 * pixels_per_pt);
        shells_[0] = shadow(0, shell_blur);
        shells_[1] = shadow(SpritePixels(8 * pixels_per_pt), shell_blur);
        menus_[0] = shadow(0, menu_blur);
        menus_[1] = shadow(SpritePixels(6 * pixels_per_pt), menu_blur);
    }
    const int pixels = SpritePixels(16 * pixels_per_pt);
    for (const auto& [id, item] : snapshot.items)
        prepare_svg(item->desc.icon, 16 * scale, pixels);
    for (const auto& menu : snapshot.layout.menus)
        prepare_svg(menu.icon, 16 * scale, pixels);
    builder_ = atlas.Builder;
    return true;
}

void AtlasAssets::prepare_feedback(const Icon& icon) {
    if (dynamic_)
        prepare_svg(icon, 16 * scale_, SpritePixels(16 * scale_ * density_));
}

ShadowSlice AtlasAssets::shadow(int radius, int blur) {
    const auto mask = MakeShadowMask(radius, blur);
    return {pack(mask.extent, mask.extent, mask.alpha.data(), false), mask.margin, mask.edge};
}

ImFontAtlasRectId AtlasAssets::pack(int width, int height, const unsigned char* pixels, bool rgba) {
    if (rgba && atlas_->TexDesiredFormat != ImTextureFormat_RGBA32) {
#ifdef HOTBOX_WITH_SPDLOG
        SPDLOG_LOGGER_ERROR(logger(), "Hotbox color icons require an RGBA32 atlas");
#endif
        return ImFontAtlasRectId_Invalid;
    }
    ImFontAtlasRect rect;
    const auto id = atlas_->AddCustomRect(width, height, &rect);
    if (id == ImFontAtlasRectId_Invalid) {
#ifdef HOTBOX_WITH_SPDLOG
        SPDLOG_LOGGER_ERROR(logger(), "Could not pack hotbox atlas rect {} × {}", width, height);
#endif
        return id;
    }
    auto& texture = *atlas_->TexData;
    for (int y = 0; y < height; ++y) {
        auto* row = static_cast<unsigned char*>(texture.GetPixelsAt(rect.x, rect.y + y));
        const auto start = Pixel(0, y, width);
        if (rgba) {
            std::memcpy(row, pixels + start * 4, static_cast<std::size_t>(width) * 4);
        } else if (texture.BytesPerPixel == 4) {
            for (int x = 0; x < width; ++x) {
                row[x * 4] = row[x * 4 + 1] = row[x * 4 + 2] = 255;
                row[x * 4 + 3] = pixels[start + static_cast<std::size_t>(x)];
            }
        } else {
            std::memcpy(row, pixels + start, static_cast<std::size_t>(width));
        }
    }
    return id;
}

void AtlasAssets::prepare_svg(const Icon& icon, float logical_size, int pixels) {
    if (icon.kind_ != Icon::Kind::Svg || !icon.vector_)
        return;
    const auto* source = icon.vector_.get();
    auto [entry, inserted] = svg_.try_emplace(source);
    const bool mask = icon.tintable_ || icon.has_color_;
    auto& variant = entry->second[mask];
    if (!inserted && variant.normal != ImFontAtlasRectId_Invalid)
        return;
    const auto& master = logical_size < 24 && !source->compact.empty() ? source->compact : source->regular;
    auto rgba = RasterSvg(master, pixels);
    if (rgba.empty()) {
#ifdef HOTBOX_WITH_SPDLOG
        SPDLOG_LOGGER_WARN(logger(), "Could not rasterize hotbox SVG icon");
#endif
        return;
    }
    if (mask) {
        std::vector<unsigned char> alpha(rgba.size() / 4);
        for (std::size_t i = 0; i < alpha.size(); ++i)
            alpha[i] = rgba[i * 4 + 3];
        variant.normal = pack(pixels, pixels, alpha.data(), false);
    } else {
        atlas_->TexPixelsUseColors = true;
        variant.normal = pack(pixels, pixels, rgba.data(), true);
        for (std::size_t i = 0; i < rgba.size(); i += 4) {
            const auto grey = static_cast<unsigned char>(
                (54u * rgba[i] + 183u * rgba[i + 1] + 19u * rgba[i + 2]) / 256u);
            rgba[i] = rgba[i + 1] = rgba[i + 2] = grey;
        }
        variant.muted = pack(pixels, pixels, rgba.data(), true);
    }
}

AtlasImage AtlasAssets::image(ImFontAtlasRectId id) const {
    ImFontAtlasRect rect;
    if (!atlas_ || id == ImFontAtlasRectId_Invalid || !atlas_->GetCustomRect(id, &rect))
        return {};
    return {atlas_->TexRef, rect.uv0, rect.uv1};
}

AtlasImage AtlasAssets::svg(const Icon& icon, bool enabled) const {
    const auto it = svg_.find(icon.vector_.get());
    if (it == svg_.end())
        return {};
    const bool mask = icon.tintable_ || icon.has_color_;
    const auto& variant = it->second[mask];
    return image(enabled || mask ? variant.normal : variant.muted);
}

bool AtlasAssets::has_icon(const Icon& icon) { return icon.kind_ != Icon::Kind::None; }

} // namespace hotbox::detail

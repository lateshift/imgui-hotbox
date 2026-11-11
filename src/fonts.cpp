#include <hotbox/fonts.hpp>
#include <hotbox/theme.hpp>

#include "log.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <span>
#include <vector>

#ifdef HOTBOX_WITH_FREETYPE
#include <imgui_freetype.h>
#endif
#ifdef HOTBOX_WITH_SPDLOG
#include <spdlog/spdlog.h>
#endif

namespace hotbox::fonts {
namespace {

using FontBytes = std::span<const unsigned char>;

std::uint32_t ReadBigEndian(FontBytes bytes) {
    std::uint32_t value = 0;
    for (const auto byte : bytes)
        value = (value << 8) | byte;
    return value;
}

bool HasBytes(FontBytes bytes, std::size_t offset, std::size_t count) {
    return offset <= bytes.size() && count <= bytes.size() - offset;
}

FontBytes FindTable(FontBytes bytes, FontBytes directory, std::uint32_t tag) {
    for (std::size_t offset = 0; offset < directory.size(); offset += 16) {
        const auto record = directory.subspan(offset, 16);
        if (ReadBigEndian(record.first(4)) == tag) {
            const auto start = ReadBigEndian(record.subspan(8, 4));
            const auto size = ReadBigEndian(record.subspan(12, 4));
            return HasBytes(bytes, start, size) ? bytes.subspan(start, size) : FontBytes{};
        }
    }
    return {};
}

int ReadSignedMetric(FontBytes bytes) {
    const int value = static_cast<int>(ReadBigEndian(bytes));
    return value < 0x8000 ? value : value - 0x10000;
}

float LineHeightPerEm(const ImFontConfig& source) {
    if (!source.FontData || source.FontDataSize < 12)
        return 1;
    const FontBytes bytes(static_cast<const unsigned char*>(source.FontData),
        static_cast<std::size_t>(source.FontDataSize));
    std::size_t face = 0;
    if (ReadBigEndian(bytes.first(4)) == 0x74746366) { // ttcf
        if (source.FontNo >= ReadBigEndian(bytes.subspan(8, 4)) ||
            source.FontNo >= (bytes.size() - 12) / 4)
            return 1;
        face = ReadBigEndian(bytes.subspan(12 + 4 * static_cast<std::size_t>(source.FontNo), 4));
    }
    if (!HasBytes(bytes, face, 12))
        return 1;
    const auto count = ReadBigEndian(bytes.subspan(face + 4, 2));
    if (!HasBytes(bytes, face + 12, count * 16))
        return 1;
    const auto directory = bytes.subspan(face + 12, count * 16);
    const auto head = FindTable(bytes, directory, 0x68656164); // head
    const auto hhea = FindTable(bytes, directory, 0x68686561); // hhea
    if (head.size() < 20 || hhea.size() < 8)
        return 1;
    const auto units_per_em = ReadBigEndian(head.subspan(18, 2));
    const int height = ReadSignedMetric(hhea.subspan(4, 2)) - ReadSignedMetric(hhea.subspan(6, 2));
    return units_per_em > 0 && height > 0
        ? static_cast<float>(height) / static_cast<float>(units_per_em) : 1;
}

#ifdef HOTBOX_EMBED_FONTS
#include "embedded_fonts.inc"
#endif

struct FontSpec {
    const char* path;
    const char* name;
    const char* role;
};

constexpr std::array specs{
    FontSpec{"inter/Inter-Regular.ttf", "Inter Regular", "type/value"},
    FontSpec{"inter/Inter-Medium.ttf", "Inter Medium", "type/label"},
    FontSpec{"inter/Inter-SemiBold.ttf", "Inter SemiBold", "type/section"},
    FontSpec{"lucide/lucide.ttf", "Lucide", "type/value"},
};

Faces FontReadError(const std::filesystem::path& path) {
#ifdef HOTBOX_WITH_SPDLOG
    SPDLOG_LOGGER_ERROR(detail::logger(), "Cannot read font {}", path.string());
#else
    (void)path;
#endif
    return {};
}

void ConfigureLoader(ImFontAtlas& atlas) {
#ifdef HOTBOX_WITH_FREETYPE
    atlas.SetFontLoader(ImGuiFreeType::GetFontLoader());
#else
    (void)atlas;
#endif
}

ImFontConfig Config(const FontSpec& spec) {
    ImFontConfig config;
    std::snprintf(config.Name, sizeof(config.Name), "%s", spec.name);
#ifdef HOTBOX_WITH_FREETYPE
    config.FontLoaderFlags = ImGuiFreeTypeLoaderFlags_HintByDensity;
#endif
    return config;
}

template<class Load>
Faces LoadFaces(ImFontAtlas& atlas, const Load& load) {
    ConfigureLoader(atlas);
    const DesignTokens tokens{};
    std::array<ImFont*, specs.size()> faces{};
    for (std::size_t index = 0; index < specs.size(); ++index) {
        const auto& spec = specs[index];
        const auto config = Config(spec);
        auto* font = load(index, 0, config);
        if (font) {
            // The atlas now owns the decompressed source. Set the reference size before baking.
            font->LegacySize = imgui_size(font, tokens.text(spec.role).size_pt);
            font->Sources[0]->SizePixels = font->LegacySize;
        }
        faces[index] = font;
    }
    return {faces[0], faces[1], faces[2], faces[3]};
}

} // namespace

float imgui_size(const ImFont* font, float em_size) {
    return font && !font->Sources.empty() ? em_size * LineHeightPerEm(*font->Sources[0]) : em_size;
}

Faces load_embedded(ImFontAtlas& atlas) {
#ifdef HOTBOX_EMBED_FONTS
    const std::array<std::span<const unsigned char>, specs.size()> data{{
        {Inter_Regular_compressed_data, Inter_Regular_compressed_size},
        {Inter_Medium_compressed_data, Inter_Medium_compressed_size},
        {Inter_SemiBold_compressed_data, Inter_SemiBold_compressed_size},
        {lucide_compressed_data, lucide_compressed_size},
    }};
    return LoadFaces(atlas, [&](std::size_t index, float size, const ImFontConfig& config) {
        return atlas.AddFontFromMemoryCompressedTTF(data[index].data(),
            static_cast<int>(data[index].size()), size, &config);
    });
#else
    (void)atlas;
    return {};
#endif
}

Faces load_from_files(ImFontAtlas& atlas, const std::filesystem::path& vendor_directory) {
    std::array<std::vector<char>, specs.size()> data;
    for (std::size_t index = 0; index < specs.size(); ++index) {
        const auto path = vendor_directory / specs[index].path;
        std::ifstream file(path, std::ios::binary);
        if (!file)
            return FontReadError(path);
        data[index].assign(std::istreambuf_iterator<char>(file), {});
        constexpr auto max_font_size = static_cast<std::size_t>(std::numeric_limits<int>::max());
        if (data[index].empty() || file.bad() || data[index].size() > max_font_size)
            return FontReadError(path);
    }
    return LoadFaces(atlas, [&](std::size_t index, float size, const ImFontConfig& config) {
        // Ownership transfers to ImGui, so the bytes persist for dynamic glyph baking.
        auto* bytes = static_cast<char*>(IM_ALLOC(data[index].size()));
        std::copy(data[index].begin(), data[index].end(), bytes);
        return atlas.AddFontFromMemoryTTF(bytes, static_cast<int>(data[index].size()), size, &config);
    });
}

} // namespace hotbox::fonts

#include <doctest/doctest.h>

#include <hotbox/fonts.hpp>
#include <hotbox/hotbox.hpp>

#include "atlas.hpp"
#include "paint.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

using namespace hotbox;
using namespace hotbox::detail;

namespace {

struct Frame {
    ImGuiContext* context = ImGui::CreateContext();

    explicit Frame(float scale) {
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {640, 480};
        io.DisplayFramebufferScale = {scale, scale};
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        auto faces = fonts::load_embedded(*io.Fonts);
        if (!faces)
            faces = fonts::load_from_files(*io.Fonts, HOTBOX_TEST_FONT_DIR);
        io.FontDefault = faces.regular;
        ApplyImGuiTheme(Theme::IFCStudioDark(), ImGui::GetStyle());
        ImGui::NewFrame();
        hotbox::begin_frame();
    }

    ~Frame() {
        ImGui::EndFrame();
        ImGui::DestroyContext(context);
    }
};

std::array<unsigned char, 4> CenterPixel(AtlasImage image) {
    auto* tex = image.texture._TexData;
    const auto x = static_cast<int>(std::floor((image.uv0.x + image.uv1.x) * 0.5f * tex->Width));
    const auto y = static_cast<int>(std::floor((image.uv0.y + image.uv1.y) * 0.5f * tex->Height));
    const auto* px = static_cast<const unsigned char*>(tex->GetPixelsAt(x, y));
    return {px[0], px[1], px[2], px[3]};
}

} // namespace

TEST_CASE("Value, caption and section paint the Inter cap height at 1x, 1.5x and 2x") {
    for (const float density : {1.0f, 1.5f, 2.0f}) {
        CAPTURE(density);
        const Frame frame(density);
        auto& draw = *ImGui::GetForegroundDrawList();
        const auto theme = Theme::IFCStudioDark();
        const PixelGrid grid{{0, 0}, {density, density}};
        const Typography type(theme, 1, density);
        const Rect bounds{{10.3f, 10.3f}, {50.3f, 34.3f}};
        const std::array roles{
            std::pair{TextRole::Value, "type/value"},
            std::pair{TextRole::Caption, "type/caption"},
            std::pair{TextRole::Section, "type/section"},
        };
        for (const auto& [role, token] : roles) {
            CAPTURE(std::string_view(token));
            const int first = draw.VtxBuffer.Size;
            type.label(draw, grid, bounds, "H", 0, IM_COL32_WHITE, role);
            REQUIRE(draw.VtxBuffer.Size - first == 4);
            const auto& top = draw.VtxBuffer[first];
            const auto& bottom = draw.VtxBuffer[first + 2];
            auto* texture = draw.CmdBuffer.back().TexRef._TexData;
            REQUIRE(texture);
            const int x0 = static_cast<int>(std::round(top.uv.x * texture->Width));
            const int x1 = static_cast<int>(std::round(bottom.uv.x * texture->Width));
            const int y0 = static_cast<int>(std::round(top.uv.y * texture->Height));
            const int y1 = static_cast<int>(std::round(bottom.uv.y * texture->Height));
            REQUIRE(y1 > y0);
            // A vertical stem covers every row. Include fractional coverage at its edges.
            float covered_rows = 0;
            for (int y = y0; y < y1; ++y) {
                unsigned char coverage = 0;
                for (int x = x0; x < x1; ++x) {
                    const auto* pixel = static_cast<const unsigned char*>(texture->GetPixelsAt(x, y));
                    coverage = std::max(coverage, pixel[texture->BytesPerPixel - 1]);
                }
                covered_rows += coverage / 255.0f;
            }
            const float height = covered_rows / (y1 - y0) * (bottom.pos.y - top.pos.y) * density;
            const float expected = theme.tokens.text(token).size_pt * (1490.0f / 2048) * density;
            CAPTURE(height);
            CAPTURE(expected);
            CHECK(std::abs(height - expected) <= 0.5f);
            const float baseline = bottom.pos.y * density;
            CHECK(std::abs(baseline - std::round(baseline)) < 0.01f);
        }
    }
}

TEST_CASE("Paint places separators on exactly one device row and column") {
    for (const float density : {1.0f, 1.5f, 2.0f}) {
        CAPTURE(density);
        const Frame frame(density);
        auto& draw = *ImGui::GetForegroundDrawList();
        const auto theme = Theme::IFCStudioDark();
        const PixelGrid grid{{0, 0}, {density, density}};
        AtlasAssets atlas;
        Painter paint(draw, theme, atlas, grid, 1);
        const auto verify = [&](Point from, Point to, bool vertical) {
            const auto start = draw.VtxBuffer.Size;
            paint.separator(from, to);
            REQUIRE(draw.VtxBuffer.Size - start == 4);
            float lo = std::numeric_limits<float>::max();
            float hi = std::numeric_limits<float>::lowest();
            for (int i = start; i < draw.VtxBuffer.Size; ++i) {
                const auto point = grid.physical({draw.VtxBuffer[i].pos.x, draw.VtxBuffer[i].pos.y});
                const float normal = vertical ? point.x : point.y;
                CHECK(normal == doctest::Approx(std::round(normal)));
                lo = std::min(lo, normal);
                hi = std::max(hi, normal);
            }
            CHECK(hi - lo == doctest::Approx(1));
        };
        verify({10.3f, 23.3f}, {40.8f, 23.3f}, false);
        verify({42.3f, 10.1f}, {42.3f, 26.1f}, true);
    }
}

TEST_CASE("A shadow mask has a continuous falloff and a transparent perimeter") {
    const auto mask = MakeShadowMask(12, 40);
    const auto at = [&](int x, int y) { return mask.alpha[static_cast<std::size_t>(y * mask.extent + x)]; };
    const int center = mask.extent / 2;
    unsigned distinct = 0;
    for (int y = 0; y < center; ++y) {
        CHECK(at(0, y) == 0);
        CHECK(at(center, y) <= at(center, y + 1));
        CHECK(at(center, y) == at(center, mask.extent - 1 - y));
        if (at(center, y) < at(center, y + 1))
            ++distinct;
    }
    CHECK(distinct > 25);
    CHECK(at(center, center) == 255);
    CHECK(at(center, mask.margin - 1) < at(center, mask.margin + 1));
}

TEST_CASE("SVG icons use optical masters by logical size and rasterize by framebuffer density") {
    constexpr auto regular = R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="32" height="32"><rect width="32" height="32" fill="red"/></svg>)svg";
    constexpr auto compact = R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16"><rect width="16" height="16" fill="blue"/></svg>)svg";
    const Frame frame(1);
    RegistrySnapshot snapshot;
    ItemDesc item;
    item.id = "icon";
    item.label = "Icon";
    item.icon = Icon::svg_pair(regular, compact).tintable(false);
    snapshot.items.emplace("icon", std::make_shared<Item>(Item{std::move(item), 1}));
    const auto& icon = snapshot.items.begin()->second->desc.icon;
    auto* fonts = ImGui::GetIO().Fonts;
    AtlasAssets atlas;
    for (const float density : {1.0f, 1.25f, 1.5f, 2.0f, 3.0f}) {
        CAPTURE(density);
        REQUIRE(atlas.prepare(*fonts, snapshot, 1, density, true));
        CHECK_FALSE(atlas.prepare(*fonts, snapshot, 1, density, true));
        const auto image = atlas.svg(icon, true);
        REQUIRE(image);
        CHECK((image.uv1.x - image.uv0.x) * image.texture._TexData->Width == doctest::Approx(16 * density));
        CHECK(CenterPixel(image) == std::array<unsigned char, 4>{0, 0, 255, 255});
        const auto disabled = CenterPixel(atlas.svg(icon, false));
        CHECK(disabled[0] == disabled[1]);
        CHECK(disabled[1] == disabled[2]);
        CHECK(disabled[3] == 255);
    }
    REQUIRE(atlas.prepare(*fonts, snapshot, 1.5f, 2, true));
    CHECK(CenterPixel(atlas.svg(icon, true)) == std::array<unsigned char, 4>{255, 0, 0, 255});
    auto& tinted = snapshot.items.begin()->second->desc.icon;
    tinted.color(IM_COL32(20, 40, 60, 255));
    ++snapshot.revision;
    REQUIRE(atlas.prepare(*fonts, snapshot, 1.5f, 2, true));
    CHECK(CenterPixel(atlas.svg(tinted, true)) == std::array<unsigned char, 4>{255, 255, 255, 255});
    atlas.release(fonts);
}

TEST_CASE("Paint submits the host texture and UVs at snapped device bounds") {
    const Frame frame(1.5f);
    auto& draw = *ImGui::GetForegroundDrawList();
    const auto theme = Theme::IFCStudioLight();
    const Typography type(theme, 1, 1.5f);
    const PixelGrid grid{{0, 0}, {1.5f, 1.5f}};
    AtlasAssets atlas;
    Painter paint(draw, theme, atlas, grid, 1);
    const ImTextureID texture = 13;
    const auto image = Icon::texture(ImTextureRef(texture), {12, 8}, {0.125f, 0.25f}, {0.5f, 0.75f});
    const auto start = draw.VtxBuffer.Size;
    paint.icon(type, {{10.2f, 10.3f}, {26.2f, 26.3f}}, image, true);
    REQUIRE(draw.VtxBuffer.Size - start == 4);
    CHECK(std::ranges::any_of(draw.CmdBuffer, [&](const auto& cmd) { return cmd.TexRef._TexID == texture; }));
    CHECK(draw.VtxBuffer[start].uv.x == 0.125f);
    CHECK(draw.VtxBuffer[start].uv.y == 0.25f);
    CHECK(grid.physical({draw.VtxBuffer[start].pos.x, draw.VtxBuffer[start].pos.y}).x == doctest::Approx(18));
}

TEST_CASE("Solid contrast passes in both modes and shapes and reports low-contrast overrides") {
    for (auto theme : {Theme::IFCStudioDark(), Theme::IFCStudioLight()}) {
        CAPTURE(theme.tokens.dark);
        for (const auto shape : {ShapeSetting::Rounded, ShapeSetting::Square}) {
            theme.shape = shape;
            REQUIRE(theme.material == Material::Solid);
            const auto checks = CheckSolidContrast(theme);
            constexpr std::array<float, 6> minimum{4.5f, 3.0f, 3.8f, 3.0f, 3.0f, 3.0f};
            for (std::size_t i = 0; i < checks.size(); ++i) {
                CAPTURE(checks[i].label);
                CAPTURE(checks[i].ratio);
                CHECK(checks[i].minimum == minimum[i]);
                CHECK(checks[i].ratio >= minimum[i]);
            }
        }
        theme.colors.ink = theme.colors.surface;
        theme.colors.pill_ink = theme.colors.surface;
        const auto body = CheckSolidContrast(theme).front();
        CHECK(body.ratio < body.minimum);
    }
}

TEST_CASE("The overlay uses the main framebuffer density when its viewport scale is unset") {
    const float density = 2;
    const Frame frame(density);
    Registry registry;
    registry.menu("cache", "Cache");
    registry.menu("help", "Help");
    Hotbox hotbox(registry);
    hotbox.set_motion({0, false});
    hotbox.open({320, 240});
    hotbox.frame();

    const auto edge = Painter::color(Theme::IFCStudioDark().colors.edge);
    const auto& vertices = ImGui::GetForegroundDrawList()->VtxBuffer;
    bool group_divider = false;
    for (int i = 0; i + 3 < vertices.Size; ++i) {
        const auto& tl = vertices[i];
        const auto& br = vertices[i + 2];
        if (tl.col == edge && vertices[i + 1].col == edge && br.col == edge && vertices[i + 3].col == edge &&
            // The row above the centre row is 46 pt up; the divider is 16 pt tall: 240 - 46 - 8 = 186.
            tl.pos.y >= 185 && tl.pos.y <= 187 &&
            (br.pos.y - tl.pos.y) * density == doctest::Approx(32) &&
            (br.pos.x - tl.pos.x) * density == doctest::Approx(1)) {
            group_divider = true;
            break;
        }
    }
    CHECK(group_divider);
}

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include <codepoints.hpp>

#include <hotbox/hotbox.hpp>
#include <imgui.h>

#include <cmath>
#include <cstring>

namespace {

float Linear(float value) {
    return value <= 0.04045f ? value / 12.92f
                            : std::pow((value + 0.055f) / 1.055f, 2.4f);
}

float Luminance(ImVec4 color) {
    return 0.2126f * Linear(color.x) + 0.7152f * Linear(color.y) + 0.0722f * Linear(color.z);
}

float Contrast(ImVec4 first, ImVec4 second) {
    const float a = Luminance(first) + 0.05f;
    const float b = Luminance(second) + 0.05f;
    return a > b ? a / b : b / a;
}

struct ImGuiContextScope {
    ImGuiContext* context = ImGui::CreateContext();
    ~ImGuiContextScope() { ImGui::DestroyContext(context); }
};

} // namespace

TEST_CASE("IFCStudio dark and light resolve typography and contrast") {
    for (const auto theme : {hotbox::Theme::IFCStudioDark(), hotbox::Theme::IFCStudioLight()}) {
        CAPTURE(theme.tokens.dark);
        CHECK(theme.tokens.text("type/value").face == "Inter-Regular");
        CHECK(theme.tokens.text("type/value").size_pt == 12);
        CHECK(theme.tokens.text("type/caption").size_pt == 11);
        CHECK(theme.tokens.text("type/section").face == "Inter-SemiBold");
        CHECK(theme.tokens.text("type/panel-title").size_pt == 16);
        const auto base = theme.tokens.color("surface/overlay");
        CHECK(Contrast(theme.colors.ink, base) >= 4.5f);
        CHECK(Contrast(theme.colors.ink, theme.colors.sector) >= 4.5f);
        CHECK(Contrast(theme.colors.ink_secondary, theme.colors.pill_hover) >= 3.0f);
        CHECK(Contrast(theme.colors.disabled, theme.colors.pill_hover) >= 3.0f);
        CHECK(Contrast(theme.colors.focus, theme.colors.sector) >= 3.0f);
        CHECK(Contrast(theme.colors.ring, base) >= 3.0f);
    }
}

TEST_CASE("Theme maps the generated colors and square geometry into ImGui") {
    auto theme = hotbox::Theme::IFCStudioLight();
    ImGuiStyle style;
    hotbox::ApplyImGuiTheme(theme, style);
    CHECK(style.FontSizeBase == theme.tokens.text("type/value").size_pt);
    CHECK(style.FrameRounding == 4);
    CHECK(style.PopupRounding == 6);
    CHECK(style.AntiAliasedFill);
    CHECK(style.AntiAliasedLines);
    CHECK(ImGui::ColorConvertFloat4ToU32(style.Colors[ImGuiCol_HeaderActive]) ==
          ImGui::ColorConvertFloat4ToU32(theme.colors.sector));
    theme.shape = hotbox::ShapeSetting::Square;
    hotbox::ApplyImGuiTheme(theme, style);
    CHECK(style.FrameRounding == 0);
    CHECK(style.PopupRounding == 0);
    CHECK(style.TabRounding == 0);
}

TEST_CASE("ImGui bakes Inter at framebuffer density with static faces") {
    const ImGuiContextScope context;
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {640, 480};
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    hotbox::fonts::Faces faces;
    SUBCASE("compiled fonts") {
        faces = hotbox::fonts::load_embedded(*io.Fonts);
#ifdef HOTBOX_TEST_EMBEDDED_FONTS
        REQUIRE(static_cast<bool>(faces));
#else
        CHECK_FALSE(static_cast<bool>(faces));
        return;
#endif
    }
    SUBCASE("files") {
        faces = hotbox::fonts::load_from_files(*io.Fonts, HOTBOX_TEST_FONT_DIR);
        REQUIRE(static_cast<bool>(faces));
    }
    CHECK(std::strcmp(faces.regular->GetDebugName(), "Inter Regular") == 0);
    CHECK(std::strcmp(faces.medium->GetDebugName(), "Inter Medium") == 0);
    CHECK(std::strcmp(faces.semibold->GetDebugName(), "Inter SemiBold") == 0);
    CHECK(std::strcmp(faces.lucide->GetDebugName(), "Lucide") == 0);
    io.FontDefault = faces.regular;
    hotbox::ApplyImGuiTheme(hotbox::Theme::IFCStudioDark(), ImGui::GetStyle());
    CHECK(ImGui::GetStyle().FontSizeBase == doctest::Approx(12.0 * (1984 + 494) / 2048));
    CHECK(faces.regular->LegacySize == ImGui::GetStyle().FontSizeBase);
    for (const float density : {1.0f, 1.5f, 2.0f}) {
        CAPTURE(density);
        io.DisplayFramebufferScale = {density, density};
        ImGui::NewFrame();
        ImGui::Begin("Text");
        CHECK(ImGui::GetFont() == faces.regular);
        CHECK(ImGui::GetFontSize() == std::round(ImGui::GetStyle().FontSizeBase));
        CHECK(ImGui::GetFontBaked()->RasterizerDensity == density);
        CHECK(ImGui::GetFontBaked()->FindGlyphNoFallback('A') != nullptr);
        ImGui::PushFont(faces.lucide, hotbox::fonts::imgui_size(faces.lucide, 16));
        CHECK(ImGui::GetFontBaked()->FindGlyphNoFallback(hotbox::lucide::icon_search) != nullptr);
        ImGui::PopFont();
        ImGui::TextUnformatted("Edit Mesh");
        ImGui::End();
        ImGui::Render();
    }
}

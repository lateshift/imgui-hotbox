#include "demo_commands.hpp"
#include "tour.hpp"
#include "wgpu_context.hpp"

#include <GLFW/glfw3.h>
#include <hotbox/hotbox.hpp>
#include <imgui_impl_glfw.h>
#include <imgui_internal.h>
#include <imgui_impl_wgpu.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string_view>

namespace {

enum class Preview {
    None, Open, Edit, Marking, OpenClose, Submenu, Cameras, MarkingInvoke, MarkingDisabled, MarkAhead, ThemeShape,
    Tour, Search, Keyboard, Recent,
};

struct Options {
    std::optional<hotbox::ThemeMode> theme;
    bool offscreen = false;
    bool motion_override = false;
    float scale = 1, time = 0;
    hotbox::ShapeSetting shape = hotbox::ShapeSetting::Rounded;
    Preview preview = Preview::None;
    hotbox::MotionPrefs motion;
    int frames = 0;
    bool public_icons = false;
    std::filesystem::path capture, sequence;
};

float ReadNumber(const char* value, std::string_view name, float minimum, float maximum) {
    char* end = nullptr;
    const float number = std::strtof(value, &end);
    if (end == value || *end || !std::isfinite(number) || number < minimum || number > maximum)
        throw std::invalid_argument("Invalid value for " + std::string(name));
    return number;
}

Options ParseOptions(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        if (index + 1 == argc)
            throw std::invalid_argument("Expected a value for " + std::string(argument));
        const std::string_view value = argv[++index];
        if (argument == "--theme" && (value == "light" || value == "dark" || value == "system")) {
            options.theme = value == "light" ? hotbox::ThemeMode::Light
                : value == "dark" ? hotbox::ThemeMode::Dark : hotbox::ThemeMode::System;
        } else if (argument == "--hotbox-preview" && (value == "open" || value == "marking")) {
            options.preview = value == "open" ? Preview::Open : Preview::Marking;
        } else if (argument == "--scenario") {
            options.offscreen = true;
            if (value == "open-layout") options.preview = Preview::Open;
            else if (value == "menu-edit") options.preview = Preview::Edit;
            else if (value == "marking-n2") options.preview = Preview::Marking;
            else if (value == "open-close") options.preview = Preview::OpenClose;
            else if (value == "submenu") options.preview = Preview::Submenu;
            else if (value == "submenu-cameras") options.preview = Preview::Cameras;
            else if (value == "marking-invoke") options.preview = Preview::MarkingInvoke;
            else if (value == "marking-disabled") options.preview = Preview::MarkingDisabled;
            else if (value == "mark-ahead") options.preview = Preview::MarkAhead;
            else if (value == "theme-shape") options.preview = Preview::ThemeShape;
            else if (value == "tour") options.preview = Preview::Tour;
            else if (value == "search") options.preview = Preview::Search;
            else if (value == "keyboard") options.preview = Preview::Keyboard;
            else if (value == "recent") options.preview = Preview::Recent;
            else throw std::invalid_argument("Unknown hotbox scenario " + std::string(value));
        } else if (argument == "--scale") {
            options.scale = ReadNumber(argv[index], argument, 0.5f, 4);
        } else if (argument == "--time") {
            options.time = ReadNumber(argv[index], argument, 0, 10);
        } else if (argument == "--motion") {
            options.motion_override = true;
            options.motion.speed = ReadNumber(argv[index], argument, 0, 2);
        } else if (argument == "--reduced-motion" && (value == "on" || value == "off")) {
            options.motion_override = true;
            options.motion.reduced_motion = value == "on";
        } else if (argument == "--shape" && (value == "rounded" || value == "square")) {
            options.shape = value == "square" ? hotbox::ShapeSetting::Square : hotbox::ShapeSetting::Rounded;
        } else if (argument == "--icons" && (value == "public" || value == "private")) {
            options.public_icons = value == "public";
        } else if (argument == "--out") {
            options.capture = value;
        } else if (argument == "--sequence") {
            options.sequence = value;
        } else if (argument == "--frames") {
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), options.frames);
            if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || options.frames < 1)
                throw std::invalid_argument("--frames requires a positive integer");
        } else {
            throw std::invalid_argument("Usage: hotbox_harness [--theme dark|light|system] [--shape rounded|square] "
                "[--scenario NAME --scale N --time S] [--motion N] [--reduced-motion on|off] "
                "[--icons public|private] [--frames N] [--out FILE | --sequence DIR]");
        }
    }
    if (options.preview == Preview::Tour && options.frames == 0)
        options.frames = harness::TourFrameCount();
    if (options.offscreen && options.capture.empty() && options.sequence.empty())
        throw std::invalid_argument("--scenario requires --out FILE or --sequence DIR");
    if (!options.sequence.empty() && (!options.offscreen || !options.capture.empty() || !options.frames))
        throw std::invalid_argument("--sequence requires --scenario and --frames, and writes every frame");
    if (!options.capture.empty() && options.frames == 0) {
        const int base = options.preview == Preview::None ? 4 : options.preview <= Preview::Marking ? 12
            : options.preview == Preview::Cameras ? 40 : 180;
        options.frames = base + static_cast<int>(std::ceil(options.time * 60));
    }
    return options;
}

struct Glfw {
    Glfw() {
        glfwSetErrorCallback([](int code, const char* message) {
            spdlog::error("GLFW error {}. {}", code, message);
        });
        if (!glfwInit())
            throw std::runtime_error("Cannot initialize GLFW");
    }

    ~Glfw() { glfwTerminate(); }
};

class ImGuiSession {
public:
    ImGuiSession() { ImGui::CreateContext(); }
    ~ImGuiSession() {
        if (renderer_)
            ImGui_ImplWGPU_Shutdown();
        if (platform_)
            ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }

    void init(GLFWwindow* window, const harness::WebGpuContext& gpu) {
        platform_ = ImGui_ImplGlfw_InitForOther(window, true);
        if (!platform_)
            throw std::runtime_error("Cannot initialize ImGui's GLFW backend");
        ImGui_ImplWGPU_InitInfo init;
        init.Device = gpu.device().Get();
        init.RenderTargetFormat = static_cast<WGPUTextureFormat>(gpu.format());
        renderer_ = ImGui_ImplWGPU_Init(&init);
        if (!renderer_)
            throw std::runtime_error("Cannot initialize ImGui's Dawn backend");
    }

private:
    bool platform_ = false;
    bool renderer_ = false;
};

/// A host-provided RGBA mask used to exercise the texture-icon path.
class TextureIcon {
public:
    TextureIcon() { ImGui::RegisterUserTexture(&texture_); }
    ~TextureIcon() {
        release();
        ImGui::UnregisterUserTexture(&texture_);
    }

    ImTextureRef ref() { return texture_.GetTexRef(); }

    void update(float pixels_per_pt) {
        const int size = std::clamp(static_cast<int>(std::lround(16 * pixels_per_pt)), 8, 128);
        if (size == size_)
            return;
        release();
        texture_.WantDestroyNextFrame = false;
        texture_.Create(ImTextureFormat_RGBA32, size, size);
        size_ = size;
        const auto edge = [](float x, float y, float start) {
            const float distance = std::max(std::abs(x - start - 0.27f), std::abs(y - start - 0.27f));
            return std::abs(distance - 0.27f) < 0.04f;
        };
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                auto* rgba = static_cast<unsigned char*>(texture_.GetPixelsAt(x, y));
                const float u = (x + 0.5f) / size, v = (y + 0.5f) / size;
                rgba[0] = rgba[1] = rgba[2] = 255;
                rgba[3] = edge(u, v, 0.12f) || edge(u, v, 0.34f) ? 255 : 0;
            }
        }
    }

private:
    void release() {
        texture_.WantDestroyNextFrame = true;
        if (texture_.BackendUserData) {
            texture_.UnusedFrames = 1;
            texture_.SetStatus(ImTextureStatus_WantDestroy);
            ImGui_ImplWGPU_UpdateTexture(&texture_);
        }
        texture_.DestroyPixels();
        texture_.SetStatus(ImTextureStatus_Destroyed);
    }

    ImTextureData texture_;
    int size_ = 0;
};

void ShowAppearance(hotbox::Hotbox& widget, hotbox::ThemeMode& mode, hotbox::Theme& light, hotbox::Theme& dark) {
    int theme_mode = static_cast<int>(mode);
    const char* themes = "Light\0Dark\0System\0";
    if (mode == hotbox::ThemeMode::System) {
        themes = widget.current_theme().tokens.dark ? "Light\0Dark\0System (Dark)\0"
            : "Light\0Dark\0System (Light)\0";
    }
    if (ImGui::Combo("Theme", &theme_mode, themes)) {
        mode = static_cast<hotbox::ThemeMode>(theme_mode);
        widget.set_theme_mode(mode);
    }
    int shape = static_cast<int>(light.shape);
    bool changed = ImGui::Combo("Shape", &shape, "Rounded\0Square\0");
    if (changed)
        light.shape = dark.shape = static_cast<hotbox::ShapeSetting>(shape);
    if (ImGui::BeginCombo("Material", "Solid")) {
        if (ImGui::Selectable("Solid", true)) {
            light.material = dark.material = hotbox::Material::Solid;
            changed = true;
        }
        ImGui::EndCombo();
    }
    if (changed)
        widget.set_theme(light, dark);

    if (ImGui::CollapsingHeader("Contrast", ImGuiTreeNodeFlags_DefaultOpen) && ImGui::BeginTable("##contrast", 3)) {
        ImGui::TableSetupColumn("Role");
        ImGui::TableSetupColumn("Ratio");
        ImGui::TableSetupColumn("Minimum");
        ImGui::TableHeadersRow();
        for (const auto& check : hotbox::CheckSolidContrast(widget.current_theme())) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(check.label.data(), check.label.data() + check.label.size());
            ImGui::TableNextColumn();
            ImGui::Text("%.2f:1%s", check.ratio, check.ratio >= check.minimum ? "" : " (low)");
            ImGui::TableNextColumn();
            ImGui::Text("%.1f:1", check.minimum);
        }
        ImGui::EndTable();
    }
}

void ShowSpecimen(const hotbox::Theme& theme, const hotbox::fonts::Faces& faces,
    const harness::WebGpuContext& gpu, std::string_view last_command,
    hotbox::Hotbox& widget, hotbox::ThemeMode& mode, hotbox::Theme& light, hotbox::Theme& dark) {
    const auto& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({24, 24}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({324, 560}, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Hotbox harness")) {
        ImGui::PushFont(faces.semibold,
            hotbox::fonts::imgui_size(faces.semibold, theme.tokens.text("type/panel-title").size_pt));
        ImGui::TextUnformatted("IFCStudio typography");
        ImGui::PopFont();
        ImGui::Spacing();

        ImGui::PushFont(faces.semibold,
            hotbox::fonts::imgui_size(faces.semibold, theme.tokens.text("type/section").size_pt));
        ImGui::TextUnformatted("Viewport");
        ImGui::PopFont();
        ImGui::TextUnformatted("Recent Commands   |   Modeling");
        ImGui::TextUnformatted("File   Edit   Create   Select   Modify");
        ImGui::TextUnformatted("Aa Bb Cc  0123456789  /  +  %");
        const auto caption = theme.tokens.text("type/caption");
        ImGui::PushFont(faces.regular, hotbox::fonts::imgui_size(faces.regular, caption.size_pt));
        ImGui::PushStyleColor(ImGuiCol_Text, caption.ink);
        ImGui::TextUnformatted("Selection  /  Edit Mesh");
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::Separator();
        ImGui::Text("%s", gpu.adapter_name().c_str());
        ImGui::Text("%.0f × %.0f px   ·   %.2f× framebuffer",
            io.DisplaySize.x * io.DisplayFramebufferScale.x,
            io.DisplaySize.y * io.DisplayFramebufferScale.y, io.DisplayFramebufferScale.x);
        ImGui::Text("%.2f× rasterizer density", ImGui::GetFontBaked()->RasterizerDensity);
        if (ImGui::Button("Open Hotbox"))
            widget.open(ImGui::GetMainViewport()->GetCenter());
        ImGui::SameLine();
#if defined(__APPLE__)
        ImGui::TextUnformatted("Hold ⌥ Space");
#else
        ImGui::TextUnformatted("Hold Alt+Space");
#endif
        if (!last_command.empty())
            ImGui::TextUnformatted(last_command.data(), last_command.data() + last_command.size());
        ShowAppearance(widget, mode, light, dark);
        auto motion = widget.motion_prefs();
        if (ImGui::SliderFloat("Motion speed", &motion.speed, 0, 2, "%.2f×"))
            widget.set_motion(motion);
        if (ImGui::Checkbox("Reduced motion", &motion.reduced_motion))
            widget.set_motion(motion);
        ImGui::SameLine();
        if (ImGui::Button("0.1× slow motion")) {
            motion.speed = 0.1f;
            motion.reduced_motion = false;
            widget.set_motion(motion);
        }
    }
    ImGui::End();

    ImGui::ShowDemoWindow();
}

void DrawViewport(const hotbox::Theme& theme) {
    auto* draw = ImGui::GetBackgroundDrawList();
    const auto size = ImGui::GetIO().DisplaySize;
    auto edge = theme.colors.edge;
    edge.w *= 0.45f;
    const auto ink = ImGui::ColorConvertFloat4ToU32(edge);
    const float horizon = size.y * 0.40f;
    for (int i = -12; i <= 12; ++i) {
        const float x = size.x * 0.5f + i * 28.0f;
        draw->AddLine({x, horizon}, {size.x * 0.5f + i * 80.0f, size.y}, ink);
    }
    for (int i = 0; i <= 8; ++i) {
        const float y = horizon + (size.y - horizon) * i * i / 64.0f;
        draw->AddLine({0, y}, {size.x, y}, ink);
    }
}

void FeedPreview(Preview preview, int rendered, ImGuiIO& io) {
    if (preview == Preview::None)
        return;
    if (preview == Preview::Tour) {
        io.DeltaTime = 1.0f / harness::tour_fps;
        const auto previous = harness::TourAt(static_cast<float>(rendered - 1) / harness::tour_fps);
        harness::FeedTour(rendered ? previous : harness::TourInput{},
            harness::TourAt(static_cast<float>(rendered) / harness::tour_fps), io);
        return;
    }
    io.DeltaTime = 1.0f / 60.0f;
    const auto press = [&](ImGuiKey key, int frame) {
        if (rendered == frame || rendered == frame + 1)
            io.AddKeyEvent(key, rendered == frame);
    };
    if (preview == Preview::Search) {
        io.AddMousePosEvent(280, 220);
        if (rendered == 3) {
            io.AddInputCharacter('d');
            io.AddInputCharacter('u');
        }
    } else if (preview == Preview::Keyboard) {
        // File, then Edit, then down its dropdown to the disabled Delete History and its tooltip.
        io.AddMousePosEvent(280, 220);
        press(ImGuiKey_Tab, 3);
        press(ImGuiKey_RightArrow, 6);
        press(ImGuiKey_Enter, 9);
        for (int step = 0; step < 5; ++step)
            press(ImGuiKey_DownArrow, 12 + 3 * step);
    } else if (preview == Preview::Recent) {
        io.AddMousePosEvent(280, 220);
        press(ImGuiKey_LeftArrow, 3);
        press(ImGuiKey_LeftArrow, 6);
        press(ImGuiKey_Enter, 9);
    } else if (preview == Preview::Open || preview == Preview::OpenClose || preview == Preview::ThemeShape) {
        io.AddMousePosEvent(280, 220);
    } else if (preview == Preview::Edit) {
        io.AddMousePosEvent(rendered < 3 ? 280.0f : 335.0f, rendered < 3 ? 220.0f : 306.0f);
        if (rendered == 1 || rendered == 2)
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, rendered == 1);
    } else if (preview == Preview::Submenu || preview == Preview::Cameras) {
        const bool closing = preview == Preview::Submenu && rendered >= 112;
        const bool over_title = rendered < 16 || closing;
        io.AddMousePosEvent(over_title ? 313.0f : 350.0f, over_title ? 260.0f : 345.0f);
        if (rendered == 1 || rendered == 2 || (closing && (rendered == 112 || rendered == 113)))
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, rendered == 1 || rendered == 112);
    } else {
        const bool disabled = preview == Preview::MarkingDisabled;
        const int move_frame = preview == Preview::MarkAhead ? 3 : 10;
        io.AddMousePosEvent(rendered >= move_frame ? (disabled ? 422.0f : 538.0f) : 480.0f,
            rendered >= move_frame ? (disabled ? 342.0f : 226.0f) : 284.0f);
        if (rendered == 1)
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        if ((preview == Preview::MarkAhead && rendered == 5) ||
            ((preview == Preview::MarkingInvoke || disabled) && rendered == 105))
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    }
}

int Run(const Options& options) {
    const Glfw glfw;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
#ifdef _WIN32
    glfwWindowHint(GLFW_WIN32_KEYBOARD_MENU, GLFW_FALSE);
#endif
    glfwWindowHint(GLFW_SCALE_FRAMEBUFFER, GLFW_TRUE);
    if (options.offscreen)
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    const std::unique_ptr<GLFWwindow, decltype(&glfwDestroyWindow)> window(
        glfwCreateWindow(960, 600, "ImGui Hotbox · Dawn", nullptr, nullptr), glfwDestroyWindow);
    if (!window)
        throw std::runtime_error("Cannot open the Hotbox harness window");

    auto light = hotbox::Theme::IFCStudioLight();
    auto dark = hotbox::Theme::IFCStudioDark();
    light.shape = dark.shape = options.shape;
    harness::WebGpuContext gpu(window.get());
    ImGuiSession imgui;
    imgui.init(window.get(), gpu);

    auto& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // The interactive harness keeps recent commands and hotbox options between runs.
    io.IniFilename = options.preview == Preview::None ? "hotbox_harness.ini" : nullptr;
    // The demo supplies its own first-use position. Seed it inside the harness window.
    ImGui::LoadIniSettingsFromMemory("[Window][Dear ImGui Demo]\n"
        "Pos=352,24\nSize=584,552\nCollapsed=0\n");
    auto faces = hotbox::fonts::load_embedded(*io.Fonts);
    if (!faces)
        faces = hotbox::fonts::load_from_files(*io.Fonts, HOTBOX_HARNESS_FONT_DIR);
    if (!faces)
        throw std::runtime_error("Cannot load the pinned Inter and Lucide fonts");
    io.FontDefault = faces.regular;
    if (options.preview != Preview::None)
        io.ConfigInputTrickleEventQueue = false;
    TextureIcon texture_icon;
    harness::DemoCommands demo(!options.public_icons);
    demo.registry.update("edit.duplicate-special", [&](auto& item) {
        item.icon = hotbox::Icon::texture(texture_icon.ref(), {16, 16});
    });
    hotbox::Hotbox widget(demo.registry);
    if (options.preview == Preview::Recent)
        widget.import_state("Recent=edit.duplicate\nRecent=edit-mesh.extrude\nRecent=view.frame\n"
            "Recent=edit.delete-history\nRecent=create.cube\n");
    widget.set_theme(light, dark);
    auto mode = options.theme.value_or(options.preview == Preview::None
        ? hotbox::ThemeMode::System : hotbox::ThemeMode::Dark);
    widget.set_theme_mode(mode);
    if (options.motion_override)
        widget.set_motion(options.motion);
    // The motion replay keeps its backdrop fixed so the overlay crossfade can be inspected.
    const auto replay_backdrop = widget.current_theme();
    if (!options.sequence.empty())
        std::filesystem::create_directories(options.sequence);

    int rendered = 0;
    float content_scale = 0;
    bool previous_dark = false;
    auto previous_shape = hotbox::ShapeSetting::Rounded;
    const bool menu_preview = options.preview == Preview::Open || options.preview == Preview::Edit ||
        options.preview == Preview::OpenClose || options.preview == Preview::Submenu ||
        options.preview == Preview::Cameras || options.preview == Preview::ThemeShape ||
        options.preview >= Preview::Search;
    while (!glfwWindowShouldClose(window.get()) && !gpu.failed()) {
        if (options.preview == Preview::None && !options.frames && !widget.is_animating())
            glfwWaitEventsTimeout(1.0 / 60);
        else
            glfwPollEvents();
        ImGui_ImplWGPU_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        if (options.offscreen) {
            io.DisplaySize = {960, 600};
            io.DisplayFramebufferScale = {options.scale, options.scale};
        }
        const float scale = options.offscreen ? 1 : ImGui_ImplGlfw_GetContentScaleForWindow(window.get());
        const auto& theme = widget.current_theme();
        if (scale != content_scale || theme.tokens.dark != previous_dark || theme.shape != previous_shape) {
            content_scale = scale;
            previous_dark = theme.tokens.dark;
            previous_shape = theme.shape;
            ImGuiStyle style;
            hotbox::ApplyImGuiTheme(theme, style);
            style.ScaleAllSizes(scale);
            style.FontScaleDpi = scale;
            ImGui::GetStyle() = style;
        }
        texture_icon.update(io.DisplayFramebufferScale.x * scale);
        FeedPreview(options.preview, rendered, io);
        ImGui::NewFrame();
        hotbox::begin_frame();
        const auto& backdrop = options.preview == Preview::ThemeShape ? replay_backdrop : theme;
        DrawViewport(backdrop);
        if (options.preview == Preview::None) {
            ShowSpecimen(theme, faces, gpu, demo.last_command, widget, mode, light, dark);
        } else if (options.preview == Preview::Tour) {
            // The tour opens the hotbox with the real trigger chord.
        } else if (rendered == 0 || (options.preview == Preview::OpenClose && rendered == 145)) {
            widget.open({480, menu_preview ? 300.0f : 440.0f});
        } else if (options.preview == Preview::OpenClose && rendered == 120) {
            widget.close();
        } else if (options.preview == Preview::ThemeShape && rendered == 120) {
            auto next_light = light;
            auto next_dark = dark;
            next_light.shape = next_dark.shape = options.shape == hotbox::ShapeSetting::Rounded
                ? hotbox::ShapeSetting::Square : hotbox::ShapeSetting::Rounded;
            widget.set_theme(next_light, next_dark);
            widget.set_theme_mode(replay_backdrop.tokens.dark ? hotbox::ThemeMode::Light : hotbox::ThemeMode::Dark);
        }
        widget.frame();
        if (options.preview == Preview::Tour) {
            harness::DrawTourOverlay(*ImGui::GetForegroundDrawList(), widget.current_theme(), faces,
                harness::TourAt(static_cast<float>(rendered) / harness::tour_fps), io.DisplaySize);
        }
        ImGui::Render();

        const bool last_frame = options.frames && rendered + 1 == options.frames;
        const auto capture = !options.sequence.empty() ? options.sequence / fmt::format("{:04}.png", rendered)
            : last_frame ? options.capture : std::filesystem::path{};
        if (!gpu.render(ImGui::GetDrawData(), backdrop.tokens.color("surface/app"), capture, options.offscreen)) {
            glfwWaitEventsTimeout(0.016);
            continue;
        }
        ++rendered;
        if (last_frame) {
            spdlog::info("Rendered {} frames at {:.2f}× framebuffer scale",
                rendered, io.DisplayFramebufferScale.x);
            if (!capture.empty())
                spdlog::info("Wrote {}", capture.string());
            break;
        }
    }
    if (gpu.failed() || (options.frames && rendered != options.frames))
        return 1;
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    try {
        spdlog::set_default_logger(spdlog::stdout_color_mt("hotbox"));
        return Run(ParseOptions(argc, argv));
    } catch (const std::exception& error) {
        spdlog::error("{}", error.what());
        return 1;
    }
}

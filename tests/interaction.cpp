#include <doctest/doctest.h>

#include <hotbox/hotbox.hpp>

#include <algorithm>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

using namespace hotbox;

namespace {

struct Script {
    ImGuiContext* context = ImGui::CreateContext();
    Registry registry;
    Hotbox box;
    int captures = 0;
    int overlay_vertices = 0;
    ImU32 first_overlay_color = 0;
    int nonbreaking_spaces = 0, literal_x = 0;
    bool text_field = false;
    bool focus_text = false;
    char text[64]{};

    explicit Script(Config config = {}) : box(registry, std::move(config)) {
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {640, 480};
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        io.AddMousePosEvent(320, 240);
        frames(2);
    }

    ~Script() { ImGui::DestroyContext(context); }

    ImGuiIO& io() { return ImGui::GetIO(); }

    void frames(int count = 1) {
        for (int i = 0; i < count; ++i) {
            io().DeltaTime = 0.01f;
            ImGui::NewFrame();
            hotbox::begin_frame();
            if (text_field) {
                ImGui::Begin("Host");
                if (focus_text) {
                    ImGui::SetKeyboardFocusHere();
                    focus_text = false;
                }
                ImGui::InputText("Name", text, sizeof(text));
                ImGui::End();
            }
            box.frame();
            const auto& vertices = ImGui::GetForegroundDrawList()->VtxBuffer;
            overlay_vertices = vertices.Size;
            first_overlay_color = vertices.empty() ? 0 : vertices.front().col;
            const auto& characters = io().InputQueueCharacters;
            nonbreaking_spaces = static_cast<int>(std::count(characters.begin(), characters.end(), ImWchar{0x00A0}));
            literal_x = static_cast<int>(std::count(characters.begin(), characters.end(), ImWchar{'x'}));
            if (io().WantCaptureMouse && io().WantCaptureKeyboard)
                ++captures;
            ImGui::Render();
        }
    }

    void chord(bool alt = true) {
        if (alt)
            io().AddKeyEvent(ImGuiMod_Alt, true);
        io().AddKeyEvent(ImGuiKey_Space, true);
        frames();
    }

    void release_alt() { io().AddKeyEvent(ImGuiMod_Alt, false); frames(); }
    void release_space() { io().AddKeyEvent(ImGuiKey_Space, false); frames(); }
    void key(ImGuiKey key) { io().AddKeyEvent(key, true); frames(); io().AddKeyEvent(key, false); frames(); }
    void move(float x, float y) { io().AddMousePosEvent(x, y); frames(); }
    void type(std::string_view text) {
        for (const char ch : text)
            io().AddInputCharacter(static_cast<unsigned char>(ch));
        frames();
    }
    void mouse(int button, bool down) { io().AddMouseButtonEvent(button, down); frames(); }
};

ItemDesc Action(std::string id, std::function<void(const InvokeContext&)> callback) {
    ItemDesc item;
    item.id = id;
    item.label = std::move(id);
    item.on_invoke = std::move(callback);
    return item;
}

} // namespace

TEST_CASE("Slow motion is interruptible without extending logical capture") {
    Script script;
    script.box.set_motion({0.1f, false});
    script.box.open({320, 240});
    script.frames(30);
    CHECK(script.box.is_open());
    CHECK(script.box.is_animating());
    CHECK(script.overlay_vertices > 0);

    script.box.close();
    CHECK_FALSE(script.box.is_open());
    script.frames(2);
    CHECK(script.box.is_animating());
    CHECK(script.overlay_vertices > 0);
    CHECK_FALSE(script.io().WantCaptureMouse);

    script.box.open({320, 240});
    script.frames();
    CHECK(script.box.is_open());
    CHECK(script.box.is_animating());
    CHECK(script.overlay_vertices > 0);

    script.box.close(false);
    script.frames();
    CHECK_FALSE(script.box.is_open());
    CHECK_FALSE(script.box.is_animating());
    CHECK(script.overlay_vertices == 0);
}

TEST_CASE("Reduced motion and zero speed cut directly to the final layout") {
    for (const MotionPrefs prefs : {MotionPrefs{1, true}, MotionPrefs{0, false}}) {
        Script script;
        script.box.set_motion(prefs);
        script.box.open({320, 240});
        script.frames();
        CHECK(script.box.is_open());
        CHECK_FALSE(script.box.is_animating());
        CHECK(script.overlay_vertices > 0);
        script.box.close();
        script.frames();
        CHECK_FALSE(script.box.is_animating());
        CHECK(script.overlay_vertices == 0);
    }
}

TEST_CASE("An idle theme retains its tween channels and theme switches can reverse") {
    Script script;
    auto dark = Theme::IFCStudioDark();
    auto light = Theme::IFCStudioLight();
    dark.colors.scrim = {1, 0, 0, 1};
    light.colors.scrim = {0, 0, 1, 1};
    script.box.set_theme(light, dark);
    script.box.open({320, 240});
    script.frames(730);
    const auto dark_scrim = script.first_overlay_color;
    CHECK((dark_scrim & IM_COL32(255, 255, 255, 0)) == IM_COL32(255, 0, 0, 0));
    CHECK_FALSE(script.box.is_animating());

    script.box.set_theme_mode(ThemeMode::Light);
    script.frames();
    CHECK(script.first_overlay_color == dark_scrim);
    script.frames(8);
    const auto midway = script.first_overlay_color;
    CHECK((midway & IM_COL32(255, 0, 0, 0)) != 0);
    CHECK((midway & IM_COL32(0, 0, 255, 0)) != 0);

    script.box.set_theme_mode(ThemeMode::Dark);
    script.frames();
    CHECK(script.first_overlay_color != dark_scrim);
    script.frames(24);
    CHECK(script.first_overlay_color == dark_scrim);
    CHECK_FALSE(script.box.is_animating());
}

TEST_CASE("System mode follows the host provider live and explicit modes take precedence") {
    Script script;
    script.box.set_motion({1, false});
    int calls = 0;
    auto appearance = ThemeMode::Dark;
    script.box.set_system_appearance_provider([&] {
        ++calls;
        return appearance;
    });
    script.box.set_theme_mode(ThemeMode::System);
    CHECK(script.box.current_theme().tokens.dark);
    script.box.open({320, 240});
    script.frames(40);
    CHECK_FALSE(script.box.is_animating());

    appearance = ThemeMode::Light;
    script.frames();
    CHECK_FALSE(script.box.current_theme().tokens.dark);
    CHECK(script.box.is_animating());
    script.frames(24);
    CHECK_FALSE(script.box.is_animating());

    script.box.set_theme_mode(ThemeMode::Dark);
    const int before = calls;
    script.frames(2);
    CHECK(calls == before);
    CHECK(script.box.current_theme().tokens.dark);
    script.box.set_theme_mode(ThemeMode::System);
    CHECK_FALSE(script.box.current_theme().tokens.dark);
}

TEST_CASE("Visible marking dispatches immediately and retains a visual tail") {
    Script script;
    int invoked = 0;
    const auto command = script.registry.add(Action("north.ne", [&](const auto&) { ++invoked; }));
    script.registry.marking_menu(Zone::North).set(Dir::NE, "north.ne");
    script.box.set_motion({0.1f, false});
    script.box.open({320, 300});
    script.move(320, 220);
    script.mouse(ImGuiMouseButton_Left, true);
    script.frames(15);
    script.move(380, 160);
    script.mouse(ImGuiMouseButton_Left, false);
    CHECK(invoked == 1);
    CHECK_FALSE(script.box.is_open());
    CHECK(script.box.is_animating());
    script.frames(250);
    CHECK(invoked == 1);
    CHECK_FALSE(script.box.is_animating());
    CHECK(script.overlay_vertices == 0);
}

TEST_CASE("Mark-ahead toast survives command removal and expires with reduced motion") {
    Script script;
    const auto command = script.registry.add(Action("north.ne", [&](const auto&) {
        CHECK(script.registry.remove("north.ne"));
    }));
    script.registry.marking_menu(Zone::North).set(Dir::NE, "north.ne");
    script.box.set_motion({1, true});
    script.box.open({320, 300});
    script.move(320, 220);
    script.mouse(ImGuiMouseButton_Left, true);
    script.move(380, 160);
    script.mouse(ImGuiMouseButton_Left, false);
    CHECK_FALSE(script.box.is_open());
    CHECK(script.box.is_animating());
    CHECK(script.overlay_vertices > 0);
    script.frames(75);
    CHECK_FALSE(script.box.is_animating());
    CHECK(script.overlay_vertices == 0);
}

TEST_CASE("Exact Alt+Space hold captures input, survives Alt release and dispatches North NE once") {
    Script script;
    int calls = 0, opens = 0, closes = 0;
    const auto command = script.registry.add(Action("north.ne", [&](const auto& ctx) {
        ++calls;
        CHECK(ctx.id == "north.ne");
        CHECK(ctx.source == InvokeSource::MarkingMenu);
        CHECK(ctx.button == ImGuiMouseButton_Left);
        CHECK(ctx.invoke_pos.x == 320);
        CHECK_FALSE(script.box.is_open());
    }));
    REQUIRE(command);
    script.registry.marking_menu(Zone::North).set(Dir::NE, "north.ne");
    script.box.on_open = [&] { ++opens; };
    script.box.on_close = [&] { ++closes; };
    script.chord();
    script.frames(17);
    CHECK_FALSE(script.box.is_open());
    script.frames();
    REQUIRE(script.box.is_open());
    CHECK(opens == 1);
    CHECK(script.captures > 0);
    script.release_alt();
    REQUIRE(script.box.is_open());
    script.move(320, 135);
    script.mouse(ImGuiMouseButton_Left, true);
    script.frames(14);
    script.move(366, 89);
    script.mouse(ImGuiMouseButton_Left, false);
    CHECK(calls == 1);
    CHECK(closes == 1);
    script.release_space();
    script.frames(5);
    CHECK(calls == 1);
    CHECK(closes == 1);
}

TEST_CASE("Plain Space and extra modifiers are excluded, short chord taps pass through") {
    Script script;
    int taps = 0;
    script.box.on_trigger_tap = [&] { ++taps; };
    script.chord(false);
    script.frames(25);
    CHECK_FALSE(script.box.is_open());
    script.release_space();
    script.io().AddKeyEvent(ImGuiMod_Shift, true);
    script.chord();
    script.frames(25);
    CHECK_FALSE(script.box.is_open());
    script.release_space();
    script.release_alt();
    script.io().AddKeyEvent(ImGuiMod_Shift, false);
    script.frames();
    script.chord();
    script.frames(2);
    script.release_space();
    CHECK(taps == 1);
    CHECK_FALSE(script.box.is_open());
}

TEST_CASE("An armed chord swallows only nonbreaking spaces and a pinned hotbox survives Space release") {
    Script script;
    script.chord();
    script.io().AddInputCharacter(0xA0);
    script.io().AddInputCharacter('x');
    script.frames();
    CHECK(script.nonbreaking_spaces == 0);
    CHECK(script.literal_x == 1);
    script.move(328, 240);
    REQUIRE(script.box.is_open());
    script.key(ImGuiKey_P);
    script.release_space();
    CHECK(script.box.is_open());
    script.key(ImGuiKey_Escape);
    CHECK_FALSE(script.box.is_open());
}

TEST_CASE("An active host text field or a can_open veto keeps the trigger closed") {
    SUBCASE("active text input") {
        Script script;
        script.text_field = script.focus_text = true;
        script.frames(3);
        script.chord();
        script.frames(25);
        CHECK_FALSE(script.box.is_open());
    }
    SUBCASE("host veto") {
        Config config;
        config.can_open = [] { return false; };
        Script script(config);
        script.chord();
        script.frames(25);
        CHECK_FALSE(script.box.is_open());
    }
    SUBCASE("host veto changes during the hold") {
        bool allow = true;
        Config config;
        config.can_open = [&] { return allow; };
        Script script(config);
        script.chord();
        script.frames(5);
        allow = false;
        script.frames(20);
        CHECK_FALSE(script.box.is_open());
    }
}

TEST_CASE("Compound mark-ahead invokes an option and delays mutations until the next frame") {
    Script script;
    int options = 0, observations = 0;
    auto action = Action("north.option", [](const auto&) { FAIL("expected the option callback"); });
    action.on_option = [&](const auto& ctx) {
        ++options;
        CHECK(ctx.option_box);
        CHECK(ctx.source == InvokeSource::MarkAhead);
        CHECK(ctx.button == ImGuiMouseButton_Right);
        CHECK(script.registry.remove(ctx.id));
        CHECK(script.registry.find(ctx.id));
        throw std::runtime_error("a host callback exception");
    };
    const auto command = script.registry.add(std::move(action));
    REQUIRE(command);
    script.registry.menu("edit", "Edit").submenu("edit.branch", "Branch",
        [](auto& menu) { menu.item("north.option"); });
    script.registry.marking_menu(Zone::North, ImGuiMouseButton_Right).set(Dir::E, "edit.branch");
    script.box.on_invoked = [&](const auto& ctx) {
        CHECK(script.registry.find(ctx.id));
        ++observations;
    };
    script.chord();
    script.frames(20);
    script.release_alt();
    REQUIRE(script.box.is_open());
    script.move(320, 140);
    script.mouse(ImGuiMouseButton_Right, true);
    script.move(374, 140);
    script.move(374, 96);
    script.io().AddKeyEvent(ImGuiMod_Shift, true);
    script.mouse(ImGuiMouseButton_Right, false);
    CHECK(options == 1);
    CHECK(observations == 1);
    CHECK(script.registry.find("north.option"));
    script.frames();
    CHECK_FALSE(script.registry.find("north.option"));
}

TEST_CASE("Releasing Space over a dropdown action invokes it once") {
    Script script;
    int calls = 0;
    const auto command = script.registry.add(Action("edit.duplicate", [&](const auto& ctx) {
        ++calls;
        CHECK(ctx.source == InvokeSource::MenuRow);
    }));
    REQUIRE(command);
    script.registry.menu("edit", "Edit").item("edit.duplicate");
    script.chord();
    script.frames(20);
    REQUIRE(script.box.is_open());
    script.move(320, 200);
    script.mouse(ImGuiMouseButton_Left, true);
    script.mouse(ImGuiMouseButton_Left, false);
    script.move(341, 231);
    script.release_space();
    script.frames(4);
    CHECK(calls == 1);
    CHECK_FALSE(script.box.is_open());
}

TEST_CASE("Disabled marking items and releases in the dead zone do not invoke") {
    Script script;
    auto action = Action("disabled", [](const auto&) { FAIL("disabled action ran"); });
    action.is_enabled = [] { return false; };
    const auto command = script.registry.add(std::move(action));
    REQUIRE(command);
    script.registry.marking_menu(Zone::North).set(Dir::NE, "disabled");
    script.box.open({320, 240});
    script.frames();
    script.move(320, 140);
    script.mouse(0, true);
    script.move(362, 98);
    script.mouse(0, false);
    CHECK(script.box.is_open());
    script.move(320, 140);
    script.mouse(0, true);
    script.frames(22);
    script.mouse(0, false);
    CHECK(script.box.is_open());
}

TEST_CASE("An open and warmed overlay makes no further ImGui heap allocations") {
    struct Count {
        ImGuiMemAllocFunc previous_alloc;
        ImGuiMemFreeFunc previous_free;
        void* previous_data;
        unsigned allocations = 0;

        Count() {
            ImGui::GetAllocatorFunctions(&previous_alloc, &previous_free, &previous_data);
            ImGui::SetAllocatorFunctions(
                [](std::size_t size, void* data) {
                    auto& self = *static_cast<Count*>(data);
                    ++self.allocations;
                    return self.previous_alloc(size, self.previous_data);
                },
                [](void* ptr, void* data) {
                    const auto& self = *static_cast<Count*>(data);
                    self.previous_free(ptr, self.previous_data);
                }, this);
        }
        ~Count() {
            ImGui::SetAllocatorFunctions(previous_alloc, previous_free, previous_data);
        }
    } count;
    Script script;
    const auto action = script.registry.add(Action("edit.duplicate", [](const auto&) {}));
    REQUIRE(action);
    script.registry.menu("edit", "Edit").item("edit.duplicate");
    script.box.open({320, 240});
    script.frames(20);
    const auto before = count.allocations;
    script.frames(120);
    CHECK(count.allocations == before);
    REQUIRE(ImGui::GetDrawData()->CmdListsCount > 0);
    unsigned commands = 0;
    for (auto* list : ImGui::GetDrawData()->CmdLists)
        commands += static_cast<unsigned>(list->CmdBuffer.Size);
    CHECK(commands <= 4);
}

TEST_CASE("A revealed marking overflow command is selectable by its box") {
    Script script;
    int calls = 0;
    const auto action = script.registry.add(Action("extra", [&](const auto& context) {
        ++calls;
        CHECK(context.source == InvokeSource::MarkingMenu);
    }));
    REQUIRE(action);
    script.registry.marking_menu(Zone::North).overflow({"extra"});
    script.box.open({320, 240});
    script.frames();
    script.move(320, 140);
    script.mouse(ImGuiMouseButton_Left, true);
    script.frames(14);
    script.move(305, 253);
    script.mouse(ImGuiMouseButton_Left, false);
    CHECK(calls == 1);
}

TEST_CASE("Releasing on a disabled overflow row does not select the underlying direction") {
    Script script;
    int calls = 0;
    auto unavailable = Action("extra", [&](const auto&) { ++calls; });
    unavailable.is_enabled = [] { return false; };
    const auto extra = script.registry.add(std::move(unavailable));
    const auto south = script.registry.add(Action("south", [&](const auto&) { ++calls; }));
    script.registry.marking_menu(Zone::North).set(Dir::S, "south").overflow({"extra"});
    script.box.open({320, 240});
    script.move(320, 140);
    script.mouse(ImGuiMouseButton_Left, true);
    script.frames(14);
    script.move(305, 269);
    script.mouse(ImGuiMouseButton_Left, false);
    CHECK(calls == 0);
    CHECK(script.box.is_open());
}

TEST_CASE("Typing searches without leaking text and keeps a released chord open until Enter") {
    Script script;
    int calls = 0;
    auto extrude = Action("mesh.extrude", [&](const auto& ctx) {
        ++calls;
        CHECK(ctx.source == InvokeSource::Search);
        CHECK_FALSE(ctx.option_box);
    });
    extrude.label = "Extrude";
    auto bevel = Action("mesh.bevel", [](const auto&) { FAIL("the wrong command ran"); });
    bevel.label = "Bevel";
    const auto first = script.registry.add(std::move(extrude));
    const auto second = script.registry.add(std::move(bevel));
    REQUIRE((first && second));
    script.registry.menu("mesh", "Mesh").item("mesh.bevel").item("mesh.extrude");
    script.chord();
    script.frames(20);
    REQUIRE(script.box.is_open());
    script.type("ex");
    CHECK(script.literal_x == 0);
    script.release_alt();
    script.release_space();
    script.frames(5);
    REQUIRE(script.box.is_open());
    script.key(ImGuiKey_Enter);
    script.frames(2);
    CHECK(calls == 1);
    CHECK_FALSE(script.box.is_open());
}

TEST_CASE("Esc clears the search, then closes the hotbox") {
    Script script;
    const auto command = script.registry.add(Action("apple", [](const auto&) { FAIL("Esc ran a command"); }));
    REQUIRE(command);
    script.box.open({320, 240});
    script.frames(5);
    script.type("ap");
    script.key(ImGuiKey_Escape);
    CHECK(script.box.is_open());
    script.key(ImGuiKey_Escape);
    CHECK_FALSE(script.box.is_open());
}

TEST_CASE("Tab, arrows and Enter run a dropdown command from the keyboard") {
    Script script;
    std::vector<std::string> ran;
    const auto record = [&](const InvokeContext& ctx) {
        ran.emplace_back(ctx.id);
        CHECK(ctx.source == InvokeSource::Keyboard);
    };
    const auto undo = script.registry.add(Action("edit.undo", record));
    const auto redo = script.registry.add(Action("edit.redo", record));
    REQUIRE((undo && redo));
    script.registry.menu("edit", "Edit").item("edit.undo").separator().item("edit.redo");
    script.box.open({320, 240});
    script.frames(20);
    script.key(ImGuiKey_Tab);   // Edit, the first title from the top.
    script.key(ImGuiKey_Enter); // Opens the dropdown on Undo.
    script.key(ImGuiKey_DownArrow);  // Skips the separator.
    script.key(ImGuiKey_Enter);
    script.frames(2);
    CHECK(ran == std::vector<std::string>{"edit.redo"});
    CHECK_FALSE(script.box.is_open());
}

TEST_CASE("Recent Commands lists invoked commands and runs them with the Recent source") {
    Script script;
    std::vector<InvokeSource> sources;
    const auto undo = script.registry.add(Action("edit.undo", [&](const auto& ctx) { sources.push_back(ctx.source); }));
    REQUIRE(undo);
    script.registry.menu("edit", "Edit").item("edit.undo");
    script.box.open({320, 240});
    script.frames(20);
    script.key(ImGuiKey_Tab);
    script.key(ImGuiKey_Enter);
    script.key(ImGuiKey_Enter);
    script.frames(30);
    REQUIRE(script.box.recent_commands().size() == 1);
    CHECK(script.box.recent_commands()[0] == "edit.undo");

    script.box.open({320, 240});
    script.frames(20);
    script.key(ImGuiKey_LeftArrow);  // Arrows start on the menu-set button.
    script.key(ImGuiKey_LeftArrow);  // Recent Commands.
    script.key(ImGuiKey_Enter);
    script.key(ImGuiKey_Enter);
    script.frames(2);
    CHECK(sources == std::vector<InvokeSource>{InvokeSource::Keyboard, InvokeSource::Recent});
}

TEST_CASE("Recent commands and the menu set persist in imgui.ini and through export_state") {
    Script script;
    const auto undo = script.registry.add(Action("edit.undo", [](const auto&) {}));
    REQUIRE(undo);
    script.registry.menu("edit", "Edit").item("edit.undo");
    script.registry.menu("mesh", "Mesh");
    script.registry.menu("rig", "Skeleton");
    script.registry.define_menu_set("Modeling", {"mesh"});
    script.registry.define_menu_set("Rigging", {"rig"});
    script.registry.set_active_menu_set("Modeling");
    script.box.open({320, 240});
    script.frames(20);
    script.key(ImGuiKey_Tab);
    script.key(ImGuiKey_Enter);
    script.key(ImGuiKey_Enter);
    script.frames(30);
    script.registry.set_active_menu_set("Rigging");
    script.frames(2);

    const std::string ini = ImGui::SaveIniSettingsToMemory();
    CHECK(ini.find("[Hotbox][Default]\n") != std::string::npos);
    CHECK(ini.find("Recent=edit.undo\n") != std::string::npos);
    CHECK(ini.find("MenuSet=Rigging\n") != std::string::npos);
    const auto exported = script.box.export_state();
    CHECK(exported.find("Rows=1\n") != std::string::npos);

    script.box.clear_history();
    CHECK(script.box.recent_commands().empty());
    script.registry.set_active_menu_set("Modeling");
    ImGui::LoadIniSettingsFromMemory(ini.c_str());
    script.frames(3);
    REQUIRE(script.box.recent_commands().size() == 1);
    CHECK(script.box.recent_commands()[0] == "edit.undo");
    CHECK(script.registry.save_layout().find("active-menu-set Rigging") != std::string::npos);

    Config config;
    config.settings_id = "Second";
    Hotbox second(script.registry, config);
    second.import_state(exported);
    REQUIRE(second.recent_commands().size() == 1);
    CHECK(second.recent_commands()[0] == "edit.undo");
    const std::string both = ImGui::SaveIniSettingsToMemory();
    CHECK(both.find("[Hotbox][Second]\n") != std::string::npos);
    CHECK(both.find("[Hotbox][Default]\n") != std::string::npos);
}

TEST_CASE("A focused command shows its tooltip after the delay") {
    Script script;
    auto item = Action("edit.undo", [](const auto&) {});
    item.tooltip = "Undo the **last** change";
    const auto command = script.registry.add(std::move(item));
    REQUIRE(command);
    script.registry.menu("edit", "Edit").item("edit.undo");
    script.box.open({320, 240});
    script.frames(20);
    script.key(ImGuiKey_Tab);
    script.key(ImGuiKey_Enter);
    script.frames(30);
    const int before = script.overlay_vertices;
    CHECK(script.box.is_animating());
    script.frames(40);
    CHECK(script.overlay_vertices > before);
}

#include "demo_commands.hpp"

#include <codepoints.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string_view>
#include <utility>

namespace harness {
namespace {

struct MenuSpec {
    const char* id;
    const char* label;
    const char* first_id;
    const char* first_label;
    const char* second_id;
    const char* second_label;
};

constexpr std::array menus{
    MenuSpec{"file", "File", "new", "New Scene", "save", "Save Scene"},
    MenuSpec{"edit", "Edit", "undo", "Undo", "redo", "Redo"},
    MenuSpec{"create", "Create", "cube", "Polygon Cube", "sphere", "Polygon Sphere"},
    MenuSpec{"select", "Select", "all", "Select All", "invert", "Invert Selection"},
    MenuSpec{"modify", "Modify", "center", "Center Pivot", "freeze", "Freeze Transformations"},
    MenuSpec{"display", "Display", "hide", "Hide Selection", "show", "Show All"},
    MenuSpec{"windows", "Windows", "outliner", "Outliner", "attributes", "Attribute Editor"},
    MenuSpec{"cache", "Cache", "create", "Create Cache", "delete", "Delete Cache"},
    MenuSpec{"help", "Help", "help", "Hotbox Help", "keys", "Keyboard Shortcuts"},
    MenuSpec{"view", "View", "perspective", "Perspective", "front", "Front"},
    MenuSpec{"shading", "Shading", "wire", "Wireframe", "smooth", "Smooth Shade"},
    MenuSpec{"lighting", "Lighting", "default", "Default Lighting", "all", "All Lights"},
    MenuSpec{"show", "Show", "grid", "Grid", "cameras", "Cameras"},
    MenuSpec{"renderer", "Renderer", "viewport", "Viewport", "quality", "High Quality"},
    MenuSpec{"panels", "Panels", "single", "Single Pane", "four", "Four Views"},
    MenuSpec{"mesh", "Mesh", "combine", "Combine", "separate", "Separate"},
    MenuSpec{"edit-mesh", "Edit Mesh", "extrude", "Extrude", "bevel", "Bevel"},
    MenuSpec{"mesh-tools", "Mesh Tools", "loop", "Insert Edge Loop", "cut", "Multi-Cut"},
    MenuSpec{"mesh-display", "Mesh Display", "soften", "Soften Edge", "harden", "Harden Edge"},
    MenuSpec{"curves", "Curves", "rebuild", "Rebuild Curve", "reverse", "Reverse Direction"},
    MenuSpec{"surfaces", "Surfaces", "loft", "Loft", "revolve", "Revolve"},
    MenuSpec{"deform", "Deform", "bend", "Bend", "twist", "Twist"},
    MenuSpec{"uv", "UV", "unwrap", "Unfold", "layout", "Layout UVs"},
    MenuSpec{"generate", "Generate", "paint", "Paint Effects", "instances", "Create Instances"},
    MenuSpec{"skeleton", "Skeleton", "joint", "Create Joints", "ik", "Create IK Handle"},
    MenuSpec{"skin", "Skin", "bind", "Bind Skin", "weights", "Paint Skin Weights"},
    MenuSpec{"constrain", "Constrain", "parent", "Parent Constraint", "aim", "Aim Constraint"},
    MenuSpec{"control", "Control", "create", "Create Control", "reset", "Reset Controls"},
};

std::string LoadSvg(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), {}};
}

/// A colour icon with a 32 px regular and a 16 px compact master.
hotbox::Icon LoadIcon(const std::filesystem::path& source, std::string_view id) {
    const auto filename = std::string(id) + ".svg";
    auto icon = hotbox::Icon::svg_pair(LoadSvg(source / "regular" / filename),
                                      LoadSvg(source / "compact" / filename));
    return icon.tintable(false);
}

hotbox::Icon DemoIcon(std::string_view name, bool private_icons) {
    (void)private_icons;
    return LoadIcon(HOTBOX_HARNESS_ICON_DIR, name);
}

#if defined(__APPLE__)
constexpr std::string_view undo_shortcut = "⌘Z", redo_shortcut = "⇧⌘Z", duplicate_shortcut = "⌘D";
constexpr std::string_view snap_shortcut = "⌥S";
#else
constexpr std::string_view undo_shortcut = "Ctrl+Z", redo_shortcut = "Ctrl+Shift+Z", duplicate_shortcut = "Ctrl+D";
constexpr std::string_view snap_shortcut = "Alt+S";
#endif

} // namespace

DemoCommands::DemoCommands(bool private_icons) {
    using namespace hotbox;
    const auto add = [&](std::string id, std::string label) {
        ItemDesc item;
        item.id = std::move(id);
        item.label = std::move(label);
        item.on_invoke = [this](const auto& ctx) { last_command = ctx.id; };
        auto handle = registry.add(std::move(item));
        handle.release();
    };
    for (const auto& menu : menus) {
        auto builder = registry.menu(menu.id, menu.label);
        for (const auto& [suffix, label] : {std::pair{menu.first_id, menu.first_label},
                                           std::pair{menu.second_id, menu.second_label}}) {
            auto id = std::string(menu.id) + "." + suffix;
            add(id, label);
            builder.item(id);
        }
    }

    add("edit.duplicate", "Duplicate");
    add("edit.duplicate-special", "Duplicate Special…");
    add("view.frame", "Frame");
    add("view.isometric", "Isometric");
    add("view.edges", "Edges");
    add("view.render-svg", "Render SVG");
    add("view.projection", "Projection");
    add("view.top", "Top");
    add("view.right", "Right");
    add("view.side", "Side");
    add("view.back", "Back");
    add("view.bottom", "Bottom");
    add("edit.delete-history", "Delete History");
    ItemDesc snap_item;
    snap_item.id = "edit.snap";
    snap_item.label = "Snap to Grid";
    snap_item.kind = ItemKind::Toggle;
    snap_item.shortcut = snap_shortcut;
    snap_item.is_checked = [this] { return snap; };
    snap_item.on_invoke = [this](const auto& ctx) { snap = !snap; last_command = ctx.id; };
    auto snap_handle = registry.add(std::move(snap_item));
    snap_handle.release();
    registry.update("edit.undo", [](auto& item) {
        item.shortcut = undo_shortcut;
        item.tooltip = "Reverse the last change to the scene.";
    });
    registry.update("edit.redo", [](auto& item) { item.shortcut = redo_shortcut; });
    registry.update("edit.duplicate", [&](auto& item) {
        item.shortcut = duplicate_shortcut;
        item.icon = DemoIcon("duplicate", private_icons);
        item.tooltip = "Copy the selected objects in place. Hold **Shift** to open the options.";
        item.keywords = {"copy", "clone"};
    });
    registry.update("edit-mesh.extrude", [](auto& item) {
        item.tooltip = "Pull the selected faces out along their normals.";
        item.keywords = {"pull", "extend"};
    });
    registry.update("create.cube", [](auto& item) { item.keywords = {"box", "primitive"}; });
    registry.update("create.sphere", [](auto& item) { item.keywords = {"ball", "primitive"}; });
    registry.update("edit.duplicate-special", [this](auto& item) {
        item.on_option = [this](const auto& ctx) { last_command = std::string(ctx.id) + " options"; };
    });
    registry.update("edit.delete-history", [](auto& item) {
        item.is_enabled = [] { return false; };
        item.disabled_reason = "The selection has no construction history.";
    });
    registry.update("view.frame", [](auto& item) {
        item.icon = Icon::glyph(lucide::icon_scan);
    });
    registry.update("view.isometric", [&](auto& item) { item.icon = DemoIcon("isometric", private_icons); });
    registry.update("view.projection", [&](auto& item) {
        item.icon = DemoIcon("perspective", private_icons);
        item.is_enabled = [] { return false; };
        item.disabled_reason = "This camera is locked.";
    });
    registry.update("view.bottom", [](auto& item) {
        item.is_enabled = [] { return false; };
        item.disabled_reason = "This viewport has no bottom camera.";
    });

    registry.menu("edit", "Edit").separator().item("edit.duplicate").item("edit.duplicate-special")
        .item("edit.snap").item("edit.delete-history");
    registry.menu("view", "View").separator().submenu("view.cameras", "Cameras", [](auto& menu) {
        menu.item("view.top").item("view.front").item("view.right")
            .submenu("view.cameras.more", "Additional Cameras", [](auto& more) {
                more.item("view.back").item("view.bottom");
            });
    });
    registry.define_menu_set("Modeling", {
        "mesh", "edit-mesh", "mesh-tools", "mesh-display", "curves", "surfaces", "deform", "uv", "generate",
        "skeleton", "skin", "deform", "constrain", "control"});
    registry.define_menu_set("Rigging", {"skeleton", "skin", "deform", "constrain", "control"});

    registry.marking_menu(Zone::North)
        .set(Dir::N, "view.frame").set(Dir::NE, "view.isometric").set(Dir::E, "view.top")
        .set(Dir::SE, "view.front").set(Dir::S, "view.right").set(Dir::SW, "view.projection")
        .set(Dir::W, "view.edges").set(Dir::NW, "view.render-svg");
    registry.marking_menu(Zone::North, ImGuiMouseButton_Right)
        .set(Dir::E, "view.cameras").set(Dir::N, "view.perspective");
    registry.marking_menu(Zone::North, ImGuiMouseButton_Middle).set(Dir::S, "view.top");
    registry.marking_menu(Zone::East).set(Dir::E, "edit-mesh.extrude").set(Dir::NE, "edit-mesh.bevel");
    registry.marking_menu(Zone::East, ImGuiMouseButton_Right).set(Dir::E, "mesh.combine");
    registry.marking_menu(Zone::East, ImGuiMouseButton_Middle).set(Dir::W, "mesh.separate");
    registry.marking_menu(Zone::South).set(Dir::S, "create.cube").set(Dir::SE, "create.sphere");
    registry.marking_menu(Zone::South, ImGuiMouseButton_Right).set(Dir::S, "select.all");
    registry.marking_menu(Zone::South, ImGuiMouseButton_Middle).set(Dir::N, "select.invert");
    registry.marking_menu(Zone::West).set(Dir::W, "edit.duplicate").set(Dir::NW, "edit.undo")
        .overflow({"edit.snap", "edit.delete-history"});
    registry.marking_menu(Zone::West, ImGuiMouseButton_Right).set(Dir::W, "display.hide");
    registry.marking_menu(Zone::West, ImGuiMouseButton_Middle).set(Dir::E, "display.show");
    registry.marking_menu(Zone::Center).set(Dir::N, "windows.outliner").set(Dir::E, "windows.attributes");
    registry.marking_menu(Zone::Center, ImGuiMouseButton_Right).set(Dir::S, "modify.center");
    registry.marking_menu(Zone::Center, ImGuiMouseButton_Middle).set(Dir::N, "modify.freeze");
}

} // namespace harness

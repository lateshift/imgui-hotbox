#include <doctest/doctest.h>

#include <hotbox/hotbox.hpp>

#include "geometry.hpp"
#include "gesture.hpp"
#include "registry_data.hpp"
#include "state.hpp"

#include <cmath>
#include <numbers>
#include <stdexcept>

using namespace hotbox;
using namespace hotbox::detail;

namespace {

Point Polar(float degrees, float distance = 50) {
    const float radians = degrees * std::numbers::pi_v<float> / 180;
    return {std::sin(radians) * distance, -std::cos(radians) * distance};
}

ItemDesc Command(std::string id, std::string label = "Action") {
    ItemDesc item;
    item.id = std::move(id);
    item.label = std::move(label);
    return item;
}

} // namespace

TEST_CASE("Zones use the press point and compass sectors retain hysteresis through north") {
    CHECK(ClassifyZone({0, -36}, 36) == Zone::Center);
    CHECK(ClassifyZone({12, -80}, 36) == Zone::North);
    CHECK(ClassifyZone({-80, -12}, 36) == Zone::West);
    CHECK(ClassifyZone({80, 12}, 36) == Zone::East);
    CHECK(ClassifyZone({12, 80}, 36) == Zone::South);
    CHECK(Config{}.dead_zone_pt == 12);
    CHECK_FALSE(Sector({0, -12}, Config{}.dead_zone_pt));
    CHECK(Sector({0, -13}, Config{}.dead_zone_pt) == Dir::N);
    CHECK(Sector(Polar(359), 10) == Dir::N);
    CHECK(Sector(Polar(337), 10) == Dir::NW);
    CHECK(Sector(Polar(337), 10, Dir::N) == Dir::N);
    CHECK(Sector(Polar(333), 10, Dir::N) == Dir::NW);
    CHECK(Sector(Polar(25), 10, Dir::N) == Dir::N);
    CHECK(Sector(Polar(27), 10, Dir::N) == Dir::NE);
}

TEST_CASE("Marking boxes fit next to each other with a fixed pivot even at work-area edges") {
    constexpr Rect work{{0, 0}, {640, 420}};
    const std::array<float, 8> widths{86, 110, 100, 120, 105, 88, 158, 80};
    for (const auto pivot : {Point{320, 210}, Point{10, 12}, Point{630, 410}}) {
        CAPTURE(pivot.x);
        CAPTURE(pivot.y);
        const auto boxes = LayoutBoxes(pivot, widths, work, 36, 92, 4);
        for (std::size_t i = 0; i < boxes.size(); ++i) {
            REQUIRE(boxes[i].has_value());
            const auto a = *boxes[i];
            CHECK(a.min.x >= work.min.x);
            CHECK(a.min.y >= work.min.y);
            CHECK(a.max.x <= work.max.x);
            CHECK(a.max.y <= work.max.y);
            CHECK(a.size().x == doctest::Approx(widths[i]));
            for (std::size_t j = 0; j < i; ++j) {
                const auto b = *boxes[j];
                CHECK((a.max.x <= b.min.x || b.max.x <= a.min.x ||
                       a.max.y <= b.min.y || b.max.y <= a.min.y));
            }
        }
    }
}

TEST_CASE("Stroke recognition rejects isolated spikes and finds compound bends") {
    Stroke stroke;
    stroke.reset({0, 0});
    stroke.add({15, 2});
    stroke.add({32, -3});
    stroke.add({30, 110});
    stroke.add({40, 1});
    stroke.add({58, 0});
    CHECK(stroke.dominant(10) == Dir::E);
    CHECK_FALSE(stroke.corner());
    stroke.reset({0, 0});
    stroke.add({15, 0});
    stroke.add({48, 0});
    stroke.add({49, -17});
    stroke.add({50, -46});
    const auto corner = stroke.corner();
    REQUIRE(corner);
    CHECK(corner->incoming == Dir::E);
    CHECK(corner->pivot == Point{48, 0});
}

TEST_CASE("Marking gesture timing handles expert, revealed, dwell and sticky modes") {
    const GestureTiming timing{Config{}.dead_zone_pt, 120, 350, 280};
    MarkGesture mark;
    std::array<bool, 8> branches{};
    mark.begin({0, 0});
    mark.step(40, {24, 0}, false, false, {}, branches, timing);
    CHECK(mark.ahead());
    CHECK_FALSE(mark.visible());
    const auto fast = mark.step(100, {60, 0}, true, false, {}, branches, timing);
    CHECK(fast.action == MarkAction::Select);
    CHECK(fast.dir == Dir::E);
    CHECK(fast.ahead);

    mark.begin({0, 0});
    mark.step(40, {24, 0}, false, false, {}, branches, timing);
    mark.step(311, {60, 0}, false, false, {}, branches, timing);
    CHECK_FALSE(mark.ahead());
    CHECK(mark.visible());

    mark.begin({0, 0});
    mark.step(70, {0, 0}, true, false, {}, branches, timing);
    CHECK(mark.sticky());
    const auto click = mark.step(10, {10, 12}, false, true, Dir::NE, branches, timing);
    CHECK(click.action == MarkAction::Select);
    CHECK(click.dir == Dir::NE);

    mark.begin({0, 0});
    branches[static_cast<unsigned>(Dir::E)] = true;
    mark.step(140, {0, 0}, false, false, {}, branches, timing);
    mark.step(10, {60, 0}, false, false, {}, branches, timing);
    const auto dwell = mark.step(280, {60, 0}, false, false, {}, branches, timing);
    CHECK(dwell.action == MarkAction::Branch);
    CHECK(dwell.pivot == Point{60, 0});
    mark.branch(dwell.pivot, {60, 0});
    branches.fill(false);
    CHECK(mark.step(20, {60, -50}, true, false, {}, branches, timing).dir == Dir::N);
}

TEST_CASE("Pure trigger dispatches hold, movement, tap and pin transitions") {
    TriggerState trigger;
    CHECK(trigger.step({0, {25, 10}, true, true, true, false, false}, 180) == Transition::None);
    CHECK(trigger.phase() == Phase::Arming);
    CHECK(trigger.step({179, {25, 10}, true, false, true, false, false}, 180) == Transition::None);
    CHECK(trigger.step({1, {25, 10}, true, false, true, false, false}, 180) == Transition::Opened);
    CHECK(trigger.step({1, {25, 10}, true, false, true, false, true}, 180) == Transition::None);
    CHECK(trigger.step({1, {25, 10}, true, false, false, false, false}, 180) == Transition::None);
    CHECK(trigger.pinned());
    CHECK(trigger.step({1, {25, 10}, true, false, false, true, false}, 180) == Transition::Closed);

    trigger.step({0, {25, 10}, true, true, true, false, false}, 180);
    CHECK(trigger.step({30, {25, 10}, true, false, false, false, false}, 180) == Transition::Tapped);
    trigger.step({0, {25, 10}, true, true, true, false, false}, 180);
    CHECK(trigger.step({1, {26, 10}, true, false, true, false, false}, 180) == Transition::Opened);
    CHECK(trigger.origin() == Point{25, 10});
}

TEST_CASE("Registry handles own their registration and deferred writes respect frame boundaries") {
    Registry registry;
    auto original = registry.add(Command("edit.duplicate"));
    REQUIRE(original);
    CHECK_FALSE(registry.add(Command("edit.duplicate")));
    CHECK(registry.remove("edit.duplicate"));
    auto replacement = registry.add(Command("edit.duplicate", "New Duplicate"));
    original = {};
    REQUIRE(registry.find("edit.duplicate"));
    CHECK(registry.find("edit.duplicate")->label == "New Duplicate");

    auto& data = RegistryAccess::get(registry);
    {
        RegistryFrame frame(data, &registry, 1);
        CHECK(registry.update("edit.duplicate", [](auto& item) { item.label = "Copy"; }));
        CHECK(registry.find("edit.duplicate")->label == "New Duplicate");
        replacement.release();
        auto temporary = registry.add(Command("edit.temp"));
        REQUIRE(temporary);
        CHECK_FALSE(registry.find("edit.temp"));
    }
    {
        RegistryFrame frame(data, &registry, 1);
        CHECK(registry.find("edit.duplicate")->label == "New Duplicate");
    }
    {
        RegistryFrame frame(data, &registry, 2);
        CHECK(registry.find("edit.duplicate")->label == "Copy");
        CHECK_FALSE(registry.find("edit.temp"));
    }
    ItemHandle orphan;
    {
        Registry temporary;
        orphan = temporary.add(Command("test.orphan"));
        REQUIRE(orphan);
    }
    CHECK_FALSE(orphan);
}

TEST_CASE("Menu and marking layouts round-trip and a parse failure keeps the previous layout") {
    Registry registry;
    registry.menu("edit", "Edit").header("Operations").item("edit.undo").separator()
        .submenu("edit.group", "Group \"Objects\"", [](auto& menu) { menu.item("edit.group.make"); });
    registry.menu("mesh", "Mesh").item("mesh.combine");
    registry.define_menu_set("Modeling", {"mesh"});
    registry.marking_menu(Zone::North).set(Dir::E, "edit.group").overflow({"edit.undo"});
    registry.marking_menu(Zone::Center, ImGuiMouseButton_Middle).set(Dir::W, "mesh.combine");
    const auto expected = registry.save_layout();
    Registry restored;
    restored.load_layout(expected);
    CHECK(restored.save_layout() == expected);
    CHECK_THROWS_AS(restored.load_layout("menu edit \"Edit\"\nsubmenu group \"Group\""), std::invalid_argument);
    CHECK(restored.save_layout() == expected);
}

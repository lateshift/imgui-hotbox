#include "registry_data.hpp"

#include <algorithm>
#include <cstdio>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace hotbox::detail {
namespace {

constexpr std::array<std::string_view, 5> zones{"north", "east", "south", "west", "center"};
constexpr std::array<std::string_view, 3> buttons{"left", "right", "middle"};
constexpr std::array<std::string_view, 8> directions{"N", "NE", "E", "SE", "S", "SW", "W", "NW"};

template<std::size_t N>
std::size_t Index(const std::array<std::string_view, N>& names, std::string_view name) {
    return static_cast<std::size_t>(std::ranges::find(names, name) - names.begin());
}

std::vector<std::string> Tokens(std::string_view line) {
    std::istringstream words{std::string(line)};
    std::vector<std::string> tokens;
    while (words >> std::ws && words.peek() != EOF) {
        if (words.peek() == '#')
            break;
        if (!(words >> std::quoted(tokens.emplace_back())))
            throw std::invalid_argument("unterminated quoted field");
    }
    return tokens;
}

void Field(std::ostream& out, const std::string& value, bool quote = false) {
    if (quote || value.empty() || value.find_first_of(" \t#\"\\") != std::string::npos)
        out << std::quoted(value);
    else
        out << value;
}

void Entries(std::ostream& out, const Layout& layout, const Menu& menu, std::size_t depth,
    std::unordered_set<std::string_view>& emitted) {
    const auto indent = std::string(depth * 2, ' ');
    for (const auto& entry : menu.entries) {
        out << indent;
        switch (entry.kind) {
        case EntryKind::Item:
            out << "item ";
            Field(out, entry.text);
            break;
        case EntryKind::Separator:
            out << "separator";
            break;
        case EntryKind::Header:
            out << "header ";
            Field(out, entry.text, true);
            break;
        case EntryKind::Submenu: {
            const auto* child = layout.find_menu(entry.text);
            if (!child || child->top_level || !emitted.insert(child->id).second) {
                out << "submenu-ref ";
                Field(out, entry.text);
                break;
            }
            out << "submenu ";
            Field(out, child->id);
            out << ' ';
            Field(out, child->label, true);
            out << '\n';
            Entries(out, layout, *child, depth + 1, emitted);
            out << indent << "end";
            break;
        }
        }
        out << '\n';
    }
}

} // namespace

Layout ParseLayout(std::string_view text) {
    Layout layout;
    std::vector<std::size_t> menu_stack;
    MarkingSlot* slot = nullptr;
    std::istringstream lines{std::string(text)};
    std::string line;
    std::size_t line_number = 0;
    const auto fail = [&](std::string_view error) -> void {
        throw std::invalid_argument("Hotbox layout line " + std::to_string(line_number) + ": " + std::string(error));
    };
    while (std::getline(lines, line)) {
        ++line_number;
        std::vector<std::string> parts;
        try { parts = Tokens(line); }
        catch (const std::invalid_argument& error) { fail(error.what()); }
        if (parts.empty())
            continue;
        const auto& op = parts[0];
        if (op == "menu" || op == "submenu") {
            const bool root = op == "menu";
            if (parts.size() != 3 || parts[1].empty() || layout.find_menu(parts[1]))
                fail("expected a unique menu id and a label");
            if (root) {
                if (menu_stack.size() > 1)
                    fail("missing submenu end");
                menu_stack.clear();
            } else {
                if (menu_stack.empty())
                    fail("submenu has no parent");
                layout.menus[menu_stack.back()].entries.push_back({EntryKind::Submenu, parts[1]});
            }
            slot = nullptr;
            menu_stack.push_back(layout.menus.size());
            layout.menus.push_back({std::move(parts[1]), std::move(parts[2]), Icon::none(), {}, root});
        } else if (op == "marking" || op == "menu-set" || op == "active-menu-set") {
            if (menu_stack.size() > 1)
                fail("missing submenu end");
            menu_stack.clear();
            slot = nullptr;
            if (op == "marking") {
                if (parts.size() != 3)
                    fail("expected marking <zone> <button>");
                const auto zone = Index(zones, parts[1]);
                const auto button = Index(buttons, parts[2]);
                if (zone == zones.size() || button == buttons.size())
                    fail("unknown marking zone or button");
                slot = &layout.marking[zone * buttons.size() + button];
            } else if (op == "menu-set") {
                if (parts.size() < 2 || parts[1].empty() ||
                    std::ranges::find(layout.sets, parts[1], &MenuSet::id) != layout.sets.end())
                    fail("expected a unique menu set id");
                layout.sets.push_back({std::move(parts[1]), {parts.begin() + 2, parts.end()}});
            } else {
                if (parts.size() != 2)
                    fail("expected active-menu-set <id>");
                layout.active_set = std::move(parts[1]);
            }
        } else if (!menu_stack.empty()) {
            auto& entries = layout.menus[menu_stack.back()].entries;
            if (op == "end" && parts.size() == 1)
                menu_stack.pop_back();
            else if (op == "separator" && parts.size() == 1)
                entries.push_back({EntryKind::Separator, {}});
            else if ((op == "item" || op == "header" || op == "submenu-ref") && parts.size() == 2)
                entries.push_back({op == "item" ? EntryKind::Item
                    : op == "header" ? EntryKind::Header : EntryKind::Submenu, std::move(parts[1])});
            else
                fail("invalid menu entry");
        } else if (slot) {
            if (op == "overflow")
                slot->overflow.assign(parts.begin() + 1, parts.end());
            else {
                const auto dir = Index(directions, op);
                if (dir == directions.size() || parts.size() != 2)
                    fail("expected a compass direction and command id");
                slot->directions[dir] = std::move(parts[1]);
            }
        } else {
            fail("expected a menu, menu set or marking slot");
        }
    }
    if (menu_stack.size() > 1)
        fail("missing submenu end");
    if (layout.active_set.empty() && !layout.sets.empty())
        layout.active_set = layout.sets.front().id;
    return layout;
}

std::string WriteLayout(const Layout& layout) {
    std::ostringstream out;
    std::unordered_set<std::string_view> emitted;
    for (const auto& menu : layout.menus) {
        if (!menu.top_level)
            continue;
        out << "menu ";
        Field(out, menu.id);
        out << ' ';
        Field(out, menu.label, true);
        out << '\n';
        Entries(out, layout, menu, 1, emitted);
    }
    for (const auto& set : layout.sets) {
        out << "menu-set ";
        Field(out, set.id);
        for (const auto& id : set.menus) {
            out << ' ';
            Field(out, id);
        }
        out << '\n';
    }
    if (!layout.active_set.empty()) {
        out << "active-menu-set ";
        Field(out, layout.active_set);
        out << '\n';
    }
    for (std::size_t i = 0; i < layout.marking.size(); ++i) {
        const auto& slot = layout.marking[i];
        if (slot.overflow.empty() && std::ranges::all_of(slot.directions, &std::string::empty))
            continue;
        out << "marking " << zones[i / buttons.size()] << ' ' << buttons[i % buttons.size()] << '\n';
        for (std::size_t dir = 0; dir < directions.size(); ++dir) {
            if (slot.directions[dir].empty())
                continue;
            out << "  " << directions[dir] << ' ';
            Field(out, slot.directions[dir]);
            out << '\n';
        }
        if (!slot.overflow.empty()) {
            out << "  overflow";
            for (const auto& id : slot.overflow) {
                out << ' ';
                Field(out, id);
            }
            out << '\n';
        }
    }
    return std::move(out).str();
}

} // namespace hotbox::detail

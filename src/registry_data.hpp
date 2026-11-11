#pragma once

#include <hotbox/registry.hpp>

#include <array>
#include <map>
#include <optional>
#include <thread>

namespace hotbox::detail {

enum class EntryKind { Item, Separator, Header, Submenu };

struct Entry {
    EntryKind kind;
    std::string text;
};

struct Menu {
    std::string id, label;
    Icon icon;
    std::vector<Entry> entries;
    bool top_level = false;
};

struct MenuSet {
    std::string id;
    std::vector<std::string> menus;
};

struct MarkingSlot {
    std::array<CommandId, 8> directions;
    std::vector<CommandId> overflow;
};

struct Layout {
    std::vector<Menu> menus;
    std::vector<MenuSet> sets;
    std::string active_set;
    std::array<MarkingSlot, 15> marking;
    Menu* find_menu(std::string_view id);
    const Menu* find_menu(std::string_view id) const;
};

struct Item {
    ItemDesc desc;
    std::uint64_t registration;
};

struct RegistrySnapshot {
    std::uint64_t revision = 0;
    std::map<CommandId, std::shared_ptr<Item>, std::less<>> items;
    Layout layout;
};

struct RegistryData {
    RegistrySnapshot current;
    std::optional<RegistrySnapshot> pending;
    std::uint64_t next_registration = 1;
    unsigned frame_depth = 0;
    const void* last_context = nullptr;
    int last_frame = -1;
#ifndef NDEBUG
    std::thread::id owner = std::this_thread::get_id();
#endif

    void check_thread() const;
    const RegistrySnapshot& projected() const;
    RegistrySnapshot& edit();
    bool remove(std::string_view id, std::uint64_t registration = 0);
};

struct RegistryAccess {
    static RegistryData& get(Registry& registry) { return *registry.data_; }
};

/// All hotboxes using this registry share one immutable snapshot during dispatch.
class RegistryFrame {
public:
    RegistryFrame(RegistryData& data, const void* context, int frame);
    ~RegistryFrame();
    RegistryFrame(const RegistryFrame&) = delete;
    RegistryFrame& operator=(const RegistryFrame&) = delete;

private:
    RegistryData& data_;
};

constexpr std::size_t SlotIndex(Zone zone, int button) {
    return static_cast<std::size_t>(zone) * 3 + static_cast<std::size_t>(button);
}

Layout ParseLayout(std::string_view text);
std::string WriteLayout(const Layout& layout);

} // namespace hotbox::detail

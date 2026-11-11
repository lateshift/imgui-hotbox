#pragma once

#include <hotbox/item.hpp>
#include <hotbox/types.hpp>

#include <cstdint>
#include <initializer_list>
#include <memory>

namespace hotbox {

namespace detail {
struct RegistryData;
struct RegistryAccess;
}

class Registry;
class MenuBuilder;
class MarkingMenuBuilder;

/// Unregisters its exact registration on destruction. May outlive the registry.
class ItemHandle {
public:
    ItemHandle() = default;
    ~ItemHandle();
    ItemHandle(ItemHandle&&) noexcept;
    ItemHandle& operator=(ItemHandle&&);
    ItemHandle(const ItemHandle&) = delete;
    ItemHandle& operator=(const ItemHandle&) = delete;

    void release();
    explicit operator bool() const;

private:
    friend class Registry;
    ItemHandle(std::weak_ptr<detail::RegistryData> data, CommandId id, std::uint64_t registration);
    void reset();

    std::weak_ptr<detail::RegistryData> data_;
    CommandId id_;
    std::uint64_t registration_ = 0;
};

/// Main-thread registration. Mutations made during a frame become visible at the next frame.
class Registry {
public:
    Registry();
    ~Registry();
    Registry(const Registry&) = delete;
    Registry& operator=(const Registry&) = delete;

    [[nodiscard]] ItemHandle add(ItemDesc desc);
    bool remove(std::string_view id);
    bool update(std::string_view id, const std::function<void(ItemDesc&)>& fn);
    const ItemDesc* find(std::string_view id) const;

    MenuBuilder menu(std::string_view menu_id, std::string label, Icon icon = Icon::none());
    void define_menu_set(std::string_view set_id, std::vector<std::string> menu_ids);
    void set_active_menu_set(std::string_view set_id);
    MarkingMenuBuilder marking_menu(Zone zone, ImGuiMouseButton button = ImGuiMouseButton_Left);

    /// Replace the layout transactionally. A malformed document throws std::invalid_argument.
    void load_layout(std::string_view text);
    std::string save_layout() const;

private:
    friend struct detail::RegistryAccess;
    std::shared_ptr<detail::RegistryData> data_;
};

/// Append entries to a named menu. Menu and item identifiers are separate namespaces.
class MenuBuilder {
public:
    MenuBuilder& item(std::string_view id);
    MenuBuilder& separator();
    MenuBuilder& header(std::string label);
    MenuBuilder& submenu(std::string_view id, std::string label,
        const std::function<void(MenuBuilder&)>& populate);

private:
    friend class Registry;
    MenuBuilder(Registry& registry, std::string id);
    Registry& registry_;
    std::string id_;
};

class MarkingMenuBuilder {
public:
    MarkingMenuBuilder& set(Dir direction, std::string_view id);
    MarkingMenuBuilder& overflow(std::vector<CommandId> ids);

private:
    friend class Registry;
    MarkingMenuBuilder(Registry& registry, Zone zone, ImGuiMouseButton button);
    Registry& registry_;
    Zone zone_;
    ImGuiMouseButton button_;
};

} // namespace hotbox

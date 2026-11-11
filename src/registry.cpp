#include "registry_data.hpp"
#include "log.hpp"

#include <algorithm>
#include <cassert>
#include <stdexcept>
#include <utility>

#ifdef HOTBOX_WITH_SPDLOG
#include <spdlog/spdlog.h>
#endif

namespace hotbox::detail {

Menu* Layout::find_menu(std::string_view id) {
    const auto found = std::ranges::find(menus, id, &Menu::id);
    return found == menus.end() ? nullptr : &*found;
}

const Menu* Layout::find_menu(std::string_view id) const {
    const auto found = std::ranges::find(menus, id, &Menu::id);
    return found == menus.end() ? nullptr : &*found;
}

void RegistryData::check_thread() const {
    assert(owner == std::this_thread::get_id() && "Hotbox registry used from another thread");
}

const RegistrySnapshot& RegistryData::projected() const {
    return pending ? *pending : current;
}

RegistrySnapshot& RegistryData::edit() {
    if (frame_depth && !pending)
        pending.emplace(current);
    auto& snapshot = pending ? *pending : current;
    ++snapshot.revision;
    return snapshot;
}

bool RegistryData::remove(std::string_view id, std::uint64_t registration) {
    check_thread();
    const auto& items = projected().items;
    const auto found = items.find(id);
    if (found == items.end() || (registration && registration != found->second->registration))
        return false;
    auto& editable = edit().items;
    editable.erase(editable.find(id));
    return true;
}

RegistryFrame::RegistryFrame(RegistryData& data, const void* context, int frame) : data_(data) {
    data_.check_thread();
    if (!data_.frame_depth && (data_.last_context != context || data_.last_frame != frame)) {
        if (data_.pending) {
            data_.current = std::move(*data_.pending);
            data_.pending.reset();
        }
        data_.last_context = context;
        data_.last_frame = frame;
    }
    ++data_.frame_depth;
}

RegistryFrame::~RegistryFrame() { --data_.frame_depth; }

} // namespace hotbox::detail

namespace hotbox {
namespace {

void RequireId(std::string_view id) {
    if (id.empty())
        throw std::invalid_argument("A hotbox identifier must not be empty");
}

detail::Menu& AddMenu(detail::Layout& layout, std::string_view id, std::string label, Icon icon, bool top_level) {
    RequireId(id);
    if (auto* menu = layout.find_menu(id)) {
        menu->label = std::move(label);
        menu->icon = std::move(icon);
        menu->top_level |= top_level;
        return *menu;
    }
    return layout.menus.emplace_back(detail::Menu{std::string(id), std::move(label), std::move(icon), {}, top_level});
}

void AppendEntry(Registry& registry, std::string_view menu_id, detail::EntryKind kind, std::string text) {
    auto& data = detail::RegistryAccess::get(registry);
    data.check_thread();
    auto* menu = data.edit().layout.find_menu(menu_id);
    if (!menu)
        throw std::logic_error("Menu builder refers to a replaced layout");
    menu->entries.push_back({kind, std::move(text)});
}

} // namespace

ItemHandle::ItemHandle(std::weak_ptr<detail::RegistryData> data, CommandId id, std::uint64_t registration)
    : data_(std::move(data)), id_(std::move(id)), registration_(registration) {}

ItemHandle::~ItemHandle() { reset(); }

ItemHandle::ItemHandle(ItemHandle&& other) noexcept
    : data_(std::move(other.data_)), id_(std::move(other.id_)),
      registration_(std::exchange(other.registration_, 0)) {}

ItemHandle& ItemHandle::operator=(ItemHandle&& other) {
    if (this != &other) {
        reset();
        data_ = std::move(other.data_);
        id_ = std::move(other.id_);
        registration_ = std::exchange(other.registration_, 0);
    }
    return *this;
}

void ItemHandle::reset() {
    if (registration_) {
        if (const auto data = data_.lock())
            data->remove(id_, registration_);
        release();
    }
}

void ItemHandle::release() {
    registration_ = 0;
    data_.reset();
    id_.clear();
}

ItemHandle::operator bool() const {
    const auto data = data_.lock();
    if (!registration_ || !data)
        return false;
    data->check_thread();
    const auto& items = data->projected().items;
    const auto found = items.find(id_);
    return found != items.end() && found->second->registration == registration_;
}

Registry::Registry() : data_(std::make_shared<detail::RegistryData>()) {}
Registry::~Registry() { data_->check_thread(); }

ItemHandle Registry::add(ItemDesc desc) {
    data_->check_thread();
    if (data_->projected().items.contains(desc.id)) {
#ifdef HOTBOX_WITH_SPDLOG
        SPDLOG_LOGGER_WARN(detail::logger(), "Duplicate hotbox command id '{}'", desc.id);
#endif
        return {};
    }
    if (desc.id.empty() || (desc.label.empty() && desc.alt_text.empty() && desc.kind != ItemKind::Separator)) {
#ifdef HOTBOX_WITH_SPDLOG
        SPDLOG_LOGGER_WARN(detail::logger(), "Hotbox command '{}' needs an id and a label or alt text", desc.id);
#endif
        return {};
    }
    const auto serial = data_->next_registration++;
    auto id = desc.id;
    auto item = std::make_shared<detail::Item>(detail::Item{std::move(desc), serial});
    data_->edit().items.emplace(id, std::move(item));
    return {data_, std::move(id), serial};
}

bool Registry::remove(std::string_view id) { return data_->remove(id); }

bool Registry::update(std::string_view id, const std::function<void(ItemDesc&)>& fn) {
    data_->check_thread();
    const auto& items = data_->projected().items;
    const auto found = items.find(id);
    if (found == items.end())
        return false;
    const auto original = found->second;
    auto replacement = std::make_shared<detail::Item>(*original);
    fn(replacement->desc);
    // The registration key remains stable across edits.
    replacement->desc.id = original->desc.id;
    if (replacement->desc.label.empty() && replacement->desc.alt_text.empty() &&
        replacement->desc.kind != ItemKind::Separator)
        return false;
    auto& editable = data_->edit().items;
    const auto slot = editable.find(original->desc.id);
    if (slot == editable.end() || slot->second != original)
        return false;
    slot->second = std::move(replacement);
    return true;
}

const ItemDesc* Registry::find(std::string_view id) const {
    data_->check_thread();
    const auto found = data_->current.items.find(id);
    return found == data_->current.items.end() ? nullptr : &found->second->desc;
}

MenuBuilder Registry::menu(std::string_view id, std::string label, Icon icon) {
    data_->check_thread();
    auto key = std::string(id);
    AddMenu(data_->edit().layout, key, std::move(label), std::move(icon), true);
    return {*this, std::move(key)};
}

void Registry::define_menu_set(std::string_view id, std::vector<std::string> menu_ids) {
    data_->check_thread();
    RequireId(id);
    auto& layout = data_->edit().layout;
    const auto found = std::ranges::find(layout.sets, id, &detail::MenuSet::id);
    if (found == layout.sets.end())
        layout.sets.push_back({std::string(id), std::move(menu_ids)});
    else
        found->menus = std::move(menu_ids);
    if (layout.active_set.empty())
        layout.active_set = id;
}

void Registry::set_active_menu_set(std::string_view id) {
    data_->check_thread();
    data_->edit().layout.active_set = id;
}

MarkingMenuBuilder Registry::marking_menu(Zone zone, ImGuiMouseButton button) {
    data_->check_thread();
    if (static_cast<unsigned>(zone) > static_cast<unsigned>(Zone::Center) ||
        button < ImGuiMouseButton_Left || button > ImGuiMouseButton_Middle)
        throw std::invalid_argument("Invalid hotbox zone or mouse button");
    return {*this, zone, button};
}

void Registry::load_layout(std::string_view text) {
    data_->check_thread();
    auto layout = detail::ParseLayout(text);
    data_->edit().layout = std::move(layout);
}

std::string Registry::save_layout() const {
    data_->check_thread();
    return detail::WriteLayout(data_->current.layout);
}

MenuBuilder::MenuBuilder(Registry& registry, std::string id) : registry_(registry), id_(std::move(id)) {}

MenuBuilder& MenuBuilder::item(std::string_view id) {
    RequireId(id);
    AppendEntry(registry_, id_, detail::EntryKind::Item, std::string(id));
    return *this;
}

MenuBuilder& MenuBuilder::separator() {
    AppendEntry(registry_, id_, detail::EntryKind::Separator, {});
    return *this;
}

MenuBuilder& MenuBuilder::header(std::string label) {
    AppendEntry(registry_, id_, detail::EntryKind::Header, std::move(label));
    return *this;
}

MenuBuilder& MenuBuilder::submenu(std::string_view id, std::string label,
    const std::function<void(MenuBuilder&)>& populate) {
    RequireId(id);
    auto key = std::string(id);
    auto& data = detail::RegistryAccess::get(registry_);
    data.check_thread();
    AddMenu(data.edit().layout, key, std::move(label), Icon::none(), false);
    AppendEntry(registry_, id_, detail::EntryKind::Submenu, key);
    MenuBuilder child(registry_, std::move(key));
    populate(child);
    return *this;
}

MarkingMenuBuilder::MarkingMenuBuilder(Registry& registry, Zone zone, ImGuiMouseButton button)
    : registry_(registry), zone_(zone), button_(button) {}

MarkingMenuBuilder& MarkingMenuBuilder::set(Dir direction, std::string_view id) {
    if (static_cast<unsigned>(direction) > static_cast<unsigned>(Dir::NW))
        throw std::invalid_argument("Invalid hotbox direction");
    auto& data = detail::RegistryAccess::get(registry_);
    data.check_thread();
    data.edit().layout.marking[detail::SlotIndex(zone_, button_)].directions[static_cast<unsigned>(direction)] = id;
    return *this;
}

MarkingMenuBuilder& MarkingMenuBuilder::overflow(std::vector<CommandId> ids) {
    auto& data = detail::RegistryAccess::get(registry_);
    data.check_thread();
    data.edit().layout.marking[detail::SlotIndex(zone_, button_)].overflow = std::move(ids);
    return *this;
}

} // namespace hotbox

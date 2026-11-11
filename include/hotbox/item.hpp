#pragma once

#include <hotbox/icon.hpp>

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace hotbox {

using CommandId = std::string;
enum class ItemKind { Action, Toggle, Radio, Submenu, Separator, Header };
enum class InvokeSource { MarkingMenu, MarkAhead, MenuRow, Search, Keyboard, Recent, Api };

struct InvokeContext {
    std::string_view id;
    InvokeSource source;
    ImGuiMouseButton button;
    ImGuiKeyChord modifiers;
    ImVec2 invoke_pos;
    bool option_box;
    void* user_data;
};

struct ItemDesc {
    CommandId id;
    std::string label;
    /// Required when `label` is empty. Searched, and shown in the tooltip when `tooltip` is empty.
    std::string alt_text;
    /// Shown after the hover delay. `**bold**` and `code` markers are drawn as plain text.
    std::string tooltip;
    /// Replaces the tooltip text while `is_enabled` returns false.
    std::string disabled_reason;
    Icon icon = Icon::none();
    /// Display only; it does not bind a key.
    std::string shortcut;
    /// Extra search terms.
    std::vector<std::string> keywords;
    ItemKind kind = ItemKind::Action;
    std::string radio_group;
    std::function<void(const InvokeContext&)> on_invoke;
    std::function<void(const InvokeContext&)> on_option;
    std::function<bool()> is_enabled;
    std::function<bool()> is_checked;
    std::function<bool()> is_visible;
    void* user_data = nullptr;
};

} // namespace hotbox

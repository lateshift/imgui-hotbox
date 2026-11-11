#include <hotbox/hotbox.hpp>

#include "overlay.hpp"
#include "gesture.hpp"
#include "log.hpp"
#include "state.hpp"
#include "system_appearance.hpp"

#include <imgui_internal.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <exception>
#include <numbers>
#include <utility>

#ifdef HOTBOX_WITH_SPDLOG
#include <spdlog/spdlog.h>
#endif

namespace hotbox {
namespace {

using namespace detail;

constexpr std::array<int, 3> mouse_buttons{
    ImGuiMouseButton_Left, ImGuiMouseButton_Right, ImGuiMouseButton_Middle,
};

Point PointOf(ImVec2 value) { return {value.x, value.y}; }

bool TextInputActive() {
    return ImGui::GetIO().WantTextInput || ImGui::GetInputTextState(ImGui::GetActiveID());
}

void Capture(ImGuiIO& io) {
    io.WantCaptureMouse = true;
    io.WantCaptureKeyboard = true;
    auto& queue = io.InputQueueCharacters;
    const auto end = std::remove(queue.begin(), queue.end(), static_cast<ImWchar>(0x00A0));
    if (end != queue.end())
        queue.resize(static_cast<int>(end - queue.begin()));
}

ImGuiViewport* ViewportAt(Point point) {
#ifdef IMGUI_HAS_VIEWPORT
    auto& io = ImGui::GetIO();
    if (const auto id = io.MouseHoveredViewport) {
        if (auto* viewport = ImGui::FindViewportByID(id)) {
            const auto min = PointOf(viewport->Pos);
            if (Rect{min, min + PointOf(viewport->Size)}.contains(point))
                return viewport;
        }
    }
    for (auto* viewport : ImGui::GetPlatformIO().Viewports) {
        const auto min = PointOf(viewport->Pos);
        if (!(viewport->Flags & ImGuiViewportFlags_NoInputs) &&
            Rect{min, min + PointOf(viewport->Size)}.contains(point))
            return viewport;
    }
#else
    (void)point;
#endif
    return ImGui::GetMainViewport();
}

#ifdef HOTBOX_WITH_SPDLOG
std::string_view SourceName(InvokeSource source) {
    switch (source) {
    case InvokeSource::MarkingMenu: return "marking-menu";
    case InvokeSource::MarkAhead: return "mark-ahead";
    case InvokeSource::MenuRow: return "menu-row";
    case InvokeSource::Search: return "search";
    case InvokeSource::Keyboard: return "keyboard";
    case InvokeSource::Recent: return "recent";
    case InvokeSource::Api: return "api";
    }
    return "unknown";
}
#endif

template<class Function, class... Args>
void Notify(const Function& fn, std::string_view id, Args&&... args) {
    if (!fn)
        return;
    try {
        fn(std::forward<Args>(args)...);
    } catch (...) {
#ifdef HOTBOX_WITH_SPDLOG
        SPDLOG_LOGGER_ERROR(logger(), "Callback failed for hotbox '{}'", id);
#else
        (void)id;
#endif
    }
}

struct Popup {
    ControlKind kind;
    std::string id;
    friend bool operator==(const Popup&, const Popup&) = default;
};

struct Mark {
    MarkGesture gesture;
    Zone zone;
    ImGuiMouseButton button;
    std::string menu;
    bool center_button;
    std::optional<double> revealed;
    Dir first = Dir::N;
};

struct Toast {
    std::shared_ptr<const Item> command;
    Point pointer;
    double expires;
};

struct Event {
    Transition transition = Transition::None;
    InvokeContext invocation{};
    const ItemDesc* item = nullptr;
    float latency_ms = 0;
};

} // namespace

struct Hotbox::Impl {
    Registry& registry;
    Config config;
    Theme light = Theme::IFCStudioLight();
    Theme dark = Theme::IFCStudioDark();
    ThemeMode theme_mode = ThemeMode::Dark;
    ThemeMode system_mode = ThemeMode::Light;
    std::function<ThemeMode()> appearance_provider;
    TriggerState trigger;
    Motion motion;
    OverlayView view, exiting_view;
    AtlasAssets atlas;
    std::vector<Popup> popups, drawn_popups, exiting_popups;
    std::optional<Mark> mark, fading_mark;
    std::optional<Toast> toast;
    std::string invoked, shaken;
    double hold_until = 0, popup_retired = 0, mark_retired = 0, rows_opened = 0;
    double popup_branch_retired = 0;
    std::size_t prior_popup_count = 0, exiting_popup_start = 0;
    bool restart_shake = false, rows_visible = false, visual_tail = false, cut_close = false;
    bool shake_in_popup = false;
    bool motion_explicit = false, pending_frame = false;
    std::vector<Event> events;
    ImGuiContext* context = nullptr;
    ImGuiID new_frame_hook = 0, shutdown_hook = 0, viewport_id = 0;
    bool view_changed = false;

    explicit Impl(Registry& registry, Config cfg) : registry(registry), config(std::move(cfg)) {
        view.reserve(layout());
        exiting_view.reserve(layout());
        events.reserve(4);
        popups.reserve(4);
        drawn_popups.reserve(4);
        exiting_popups.reserve(4);
    }

    ~Impl() {
        if (context) {
            atlas.release(context->IO.Fonts);
            ImGui::RemoveContextHook(context, new_frame_hook);
            ImGui::RemoveContextHook(context, shutdown_hook);
        }
    }

    const Layout& layout() const { return RegistryAccess::get(registry).current.layout; }
    bool is_open() const { return trigger.phase() == Phase::Open; }
    const Theme& resolved_theme() const {
        return (theme_mode == ThemeMode::System ? system_mode : theme_mode) == ThemeMode::Dark ? dark : light;
    }

    void refresh_appearance() {
        if (theme_mode != ThemeMode::System)
            return;
        auto mode = ThemeMode::System;
        if (appearance_provider) {
            try {
                mode = appearance_provider();
            } catch (...) {
#ifdef HOTBOX_WITH_SPDLOG
                SPDLOG_LOGGER_ERROR(logger(), "Hotbox system appearance provider failed");
#endif
            }
        }
        system_mode = mode == ThemeMode::System ? SystemAppearance() : mode;
    }

    void attach() {
        if (context) {
            assert(context == ImGui::GetCurrentContext());
            return;
        }
        context = ImGui::GetCurrentContext();
        ImGuiContextHook hook;
        hook.UserData = this;
        hook.Type = ImGuiContextHookType_NewFramePost;
        hook.Callback = [](ImGuiContext* ctx, ImGuiContextHook* entry) {
            const auto& self = *static_cast<Impl*>(entry->UserData);
            if (self.trigger.phase() != Phase::Closed)
                Capture(ctx->IO);
            if (self.is_open()) {
                for (const auto button : mouse_buttons)
                    ImGui::SetKeyOwner(ImGui::MouseButtonToKey(button), entry->HookId, ImGuiInputFlags_LockThisFrame);
            }
        };
        new_frame_hook = ImGui::AddContextHook(context, &hook);
        hook.Type = ImGuiContextHookType_Shutdown;
        hook.Callback = [](ImGuiContext*, ImGuiContextHook* entry) {
            auto& self = *static_cast<Impl*>(entry->UserData);
            self.atlas.forget();
            self.context = nullptr;
            self.viewport_id = 0;
            self.visual_tail = false;
            self.toast.reset();
        };
        shutdown_hook = ImGui::AddContextHook(context, &hook);
    }

    bool can_open() const {
        try {
            return !config.can_open || config.can_open();
        } catch (...) {
#ifdef HOTBOX_WITH_SPDLOG
            SPDLOG_LOGGER_ERROR(logger(), "Hotbox can_open callback failed");
#endif
            return false;
        }
    }

    void signal(Transition transition) {
        if (transition != Transition::None)
            events.push_back({transition});
    }

    void retire_mark() {
        if (mark && mark->gesture.visible() && motion.enabled() && !cut_close) {
            fading_mark = std::move(mark);
            mark_retired = motion.time();
        }
        view_changed |= mark.has_value();
        mark.reset();
    }

    void clear_session() {
        retire_mark();
        view_changed |= !popups.empty();
        popups.clear();
    }

    void close(bool animated = true) {
        cut_close |= !animated;
        signal(trigger.close());
        clear_session();
        pending_frame = true;
    }

    void shake(const Control& control, bool in_popup = false) {
        if (!control.enabled && !control.id.empty() && motion.enabled()) {
            shaken = control.id;
            restart_shake = true;
            shake_in_popup = in_popup;
        }
    }

    void enqueue(const Control& command, InvokeSource source, int button, bool option) {
        if (!command.enabled || !command.item)
            return;
        const auto& item = *command.item;
        option = option && static_cast<bool>(item.on_option);
        if (!(option ? item.on_option : item.on_invoke))
            return;
        const auto origin = trigger.origin();
        events.push_back({Transition::None,
            {item.id, source, button, ImGui::GetIO().KeyMods, {origin.x, origin.y}, option, item.user_data},
            &item, trigger.latency_ms()});
        const auto& snapshot = RegistryAccess::get(registry).current;
        if (source == InvokeSource::MarkAhead) {
            const auto command_item = snapshot.items.find(item.id);
            if (command_item != snapshot.items.end())
                toast = Toast{command_item->second, PointOf(ImGui::GetIO().MousePos), ImGui::GetTime() + 0.7};
        } else if (motion.enabled()) {
            invoked = item.id;
            hold_until = motion.time() + resolved_theme().motion.hover;
        }
        if (!trigger.pinned())
            signal(trigger.close());
        clear_session();
    }

    void dispatch(Hotbox& widget) {
        const auto count = events.size();
        for (std::size_t i = 0; i < count; ++i) {
            const auto event = events[i];
            switch (event.transition) {
            case Transition::Opened:
#ifdef HOTBOX_WITH_SPDLOG
                SPDLOG_LOGGER_INFO(logger(), "Hotbox opened");
#endif
                Notify(std::function(widget.on_open), "on_open");
                break;
            case Transition::Closed:
#ifdef HOTBOX_WITH_SPDLOG
                SPDLOG_LOGGER_INFO(logger(), "Hotbox closed");
#endif
                Notify(std::function(widget.on_close), "on_close");
                break;
            case Transition::Tapped:
                Notify(std::function(widget.on_trigger_tap), "on_trigger_tap");
                break;
            case Transition::None:
#ifdef HOTBOX_WITH_SPDLOG
                SPDLOG_LOGGER_INFO(logger(), "Hotbox invoked '{}' via {} in {:.1f} ms",
                    event.invocation.id, SourceName(event.invocation.source), event.latency_ms);
#endif
                Notify(event.invocation.option_box ? event.item->on_option : event.item->on_invoke,
                    event.invocation.id, event.invocation);
                Notify(std::function(widget.on_invoked), event.invocation.id, event.invocation);
                break;
            }
        }
        events.erase(events.begin(), events.begin() + static_cast<std::ptrdiff_t>(count));
    }

    std::span<const CommandId> mark_overflow(const Mark& marking) const {
        if (!marking.menu.empty())
            return {};
        return layout().marking[SlotIndex(marking.zone, marking.button)].overflow;
    }

    bool has_mark_overflow(const Mark& marking) const {
        if (marking.menu.empty())
            return !mark_overflow(marking).empty();
        const auto* menu = layout().find_menu(marking.menu);
        return menu && std::ranges::count_if(menu->entries, [](const auto& entry) {
            return entry.kind == EntryKind::Item || entry.kind == EntryKind::Submenu;
        }) > 8;
    }

    std::array<Control, 8> mark_choices(const Mark& marking) const {
        std::array<Control, 8> choices{};
        if (marking.menu.empty()) {
            const auto& directions = layout().marking[SlotIndex(marking.zone, marking.button)].directions;
            for (std::size_t i = 0; i < choices.size(); ++i) {
                if (!directions[i].empty())
                    choices[i] = Resolve(registry, layout(), directions[i]);
            }
        } else if (const auto* menu = layout().find_menu(marking.menu)) {
            std::size_t i = 0;
            for (const auto& entry : menu->entries) {
                if (i == choices.size())
                    break;
                if (entry.kind == EntryKind::Item || entry.kind == EntryKind::Submenu) {
                    auto choice = Resolve(registry, layout(), entry.text, entry.kind == EntryKind::Submenu);
                    choices[i++] = choice;
                }
            }
        }
        for (auto& choice : choices) {
            if (choice.kind != ControlKind::Command && choice.kind != ControlKind::Submenu)
                choice = {};
        }
        return choices;
    }

    void begin_mark(Point pointer, Zone zone, ImGuiMouseButton button, bool center_button) {
        clear_session();
        mark.emplace(Mark{MarkGesture{}, zone, button, {}, center_button, {}, Dir::N});
        mark->gesture.begin(pointer);
        view_changed = true;
#ifdef HOTBOX_WITH_SPDLOG
        SPDLOG_LOGGER_DEBUG(logger(), "Hotbox marking stroke started in zone {}, button {}",
            static_cast<int>(zone), button);
#endif
    }

    void branch_mark(std::string_view menu, Point pivot, Point pointer, bool sticky = false) {
        if (mark->revealed && mark->menu != menu && motion.enabled()) {
            fading_mark = *mark;
            mark_retired = motion.time();
        }
        mark->menu = menu;
        mark->gesture.branch(pivot, pointer, sticky);
        mark->revealed.reset();
        view_changed = true;
    }

    ImGuiID mark_scope(const Mark& marking) const {
        return motion.owner(marking.menu, static_cast<ImGuiID>(SlotIndex(marking.zone, marking.button)));
    }

    void build_mark(OverlayView& target, const Mark& marking, const std::array<Control, 8>& choices) {
        target.marking(marking.gesture.pivot(), choices);
        if (has_mark_overflow(marking))
            target.popup(registry, layout(), ControlKind::Overflow, marking.menu,
                target.overflow_anchor(marking.gesture.pivot()), 0, mark_overflow(marking), mark_scope(marking));
    }

    void mark_step(Point pointer, float dt, float scale, const ImGuiIO& io,
        std::array<Control, 8>& choices) {
        const int button = mark->button;
        const bool option = io.KeyShift;
        const bool released = io.MouseReleased[button];
        const bool pressed = io.MouseClicked[button];
        const bool was_sticky = mark->gesture.sticky();
        if ((released || pressed) && mark->gesture.visible()) {
            if (const auto* overflow = view.popup_item(pointer)) {
                if (overflow->kind == ControlKind::Submenu && overflow->enabled && mark->gesture.can_branch()) {
                    branch_mark(overflow->id, pointer, pointer, true);
                    choices = mark_choices(*mark);
                    return;
                }
                if (overflow->kind == ControlKind::Command && overflow->enabled) {
                    enqueue(*overflow, InvokeSource::MarkingMenu, button, option);
                    return;
                }
                if (!overflow->enabled &&
                    (overflow->kind == ControlKind::Command || overflow->kind == ControlKind::Submenu)) {
                    if (released) {
                        shake(*overflow, true);
                        retire_mark();
                    }
                    return;
                }
            }
        }
        const auto box_hit = was_sticky ? view.mark_at(pointer) : std::nullopt;
        for (std::size_t i = 0; i < choices.size() && mark->gesture.visible(); ++i) {
            if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_1 + i), false)) {
                if (choices[i].kind == ControlKind::Submenu && choices[i].enabled && mark->gesture.can_branch()) {
                    branch_mark(choices[i].id, pointer, pointer);
                    choices = mark_choices(*mark);
                } else {
                    enqueue(choices[i], InvokeSource::Keyboard, button, option);
                }
                return;
            }
        }
        const GestureTiming timing{config.dead_zone_pt * scale, config.reveal_delay_ms,
            config.markahead_window_ms, config.submenu_dwell_ms};
        for (;;) {
            std::array<bool, 8> submenus{};
            for (std::size_t i = 0; i < submenus.size(); ++i)
                submenus[i] = choices[i].kind == ControlKind::Submenu && choices[i].enabled;
            const auto result = mark->gesture.step(dt, pointer, released, pressed, box_hit, submenus, timing);
            if (result.action == MarkAction::Branch) {
                branch_mark(choices[static_cast<unsigned>(result.dir)].id, result.pivot, pointer);
                choices = mark_choices(*mark);
#ifdef HOTBOX_WITH_SPDLOG
                SPDLOG_LOGGER_DEBUG(logger(), "Hotbox marking branch {} via sector {}",
                    mark->menu, static_cast<int>(result.dir));
#endif
                if (released && !was_sticky) {
                    dt = 0;
                    continue;
                }
            } else if (result.action == MarkAction::Select) {
                const auto& choice = choices[static_cast<unsigned>(result.dir)];
                const auto source = result.ahead ? InvokeSource::MarkAhead : InvokeSource::MarkingMenu;
                if (!result.ahead)
                    shake(choice);
                enqueue(choice, source, button, option);
                retire_mark();
            } else if (result.action == MarkAction::Cancel) {
                retire_mark();
            } else if (mark->center_button && !was_sticky && mark->gesture.sticky()) {
                const auto* title = view.title(pointer);
                if (title && title->kind == ControlKind::Sets) {
                    retire_mark();
                    popups.push_back({ControlKind::Sets, {}});
                    view_changed = true;
                }
            }
            break;
        }
    }

    std::size_t build_popups(OverlayView& target, std::span<const Popup> list) {
        for (std::size_t i = 0; i < list.size(); ++i) {
            const auto& popup = list[i];
            const auto* anchor = target.anchor(popup.kind, popup.id, i);
            if (!anchor)
                return i;
            const auto rect = anchor->rect;
            target.popup(registry, layout(), popup.kind, popup.id, rect, i);
        }
        return list.size();
    }

    void open_title(const Control& title) {
        view_changed = true;
        const bool same = !popups.empty() && popups[0].kind == title.kind && popups[0].id == title.id;
        popups.clear();
        if (!same)
            popups.push_back({title.kind, std::string(title.id)});
    }

    void hover_submenu(const Control& item) {
        const auto level = item.level + 1;
        if (!item.enabled || item.kind != ControlKind::Submenu || item.id.empty())
            return;
        if (level < popups.size() && popups[level].id == item.id)
            return;
        if (std::ranges::any_of(popups, [&](const auto& popup) { return popup.id == item.id; }))
            return;
        view_changed = true;
        popups.resize(level);
        popups.push_back({ControlKind::Submenu, std::string(item.id)});
    }

    void popup_click(const Control& item, Point pointer, float scale) {
        if (!item.enabled)
            return;
        switch (item.kind) {
        case ControlKind::Command:
            enqueue(item, InvokeSource::MenuRow, ImGuiMouseButton_Left,
                pointer.x >= item.rect.max.x - 22 * scale);
            break;
        case ControlKind::ChooseSet:
            registry.set_active_menu_set(item.id);
            popups.clear();
            view_changed = true;
            break;
        case ControlKind::Pin:
            trigger.set_pinned(!trigger.pinned());
            popups.clear();
            view_changed = true;
            break;
        case ControlKind::Rows:
            config.show_menu_rows = !config.show_menu_rows;
            popups.clear();
            view_changed = true;
            break;
        case ControlKind::Ticks:
            config.show_zone_ticks = !config.show_zone_ticks;
            popups.clear();
            view_changed = true;
            break;
        default: break;
        }
    }

    void idle_input(Point pointer, const ImGuiIO& io, float scale) {
        const auto* hit = view.popup_item(pointer);
        if (hit) {
            if (hit->kind == ControlKind::Submenu)
                hover_submenu(*hit);
            else if (hit->level + 1 < popups.size()) {
                popups.resize(hit->level + 1);
                view_changed = true;
            }
            if (io.MouseClicked[ImGuiMouseButton_Left])
                popup_click(*hit, pointer, scale);
            if (io.MouseReleased[ImGuiMouseButton_Left] &&
                hit->rect.contains(PointOf(io.MouseClickedPos[ImGuiMouseButton_Left])))
                shake(*hit, true);
            return;
        }
        const auto* title = view.title(pointer);
        for (const int button : mouse_buttons) {
            if (!io.MouseClicked[button])
                continue;
            const auto zone = ClassifyZone(pointer - trigger.origin(), 36 * scale);
            const auto& slot = layout().marking[SlotIndex(zone, button)];
            const bool has_mark = !slot.overflow.empty() ||
                std::ranges::any_of(slot.directions, [](const auto& id) { return !id.empty(); });
            const bool center_button = button == ImGuiMouseButton_Left && title &&
                title->kind == ControlKind::Sets && zone == Zone::Center;
            if (button == ImGuiMouseButton_Left && title && title->enabled && !(center_button && has_mark))
                open_title(*title);
            else if (has_mark)
                begin_mark(pointer, zone, button, center_button);
            else if (!view.on_chrome(pointer))
                close();
            return;
        }
    }

    float shake_offset(const Theme& theme) {
        if (shaken.empty())
            return 0;
        if (!motion.enabled() || cut_close) {
            shaken.clear();
            restart_shake = false;
            return 0;
        }
        const auto owner = motion.owner(shaken);
        if (std::exchange(restart_shake, false))
            motion.tween(owner, "shake", 0, 0, iam_ease_linear, 1, true);
        const float phase = motion.tween(owner, "shake", 1, 2 * theme.motion.hover, iam_ease_linear);
        if (phase >= 1) {
            shaken.clear();
            if (fading_mark)
                mark_retired = motion.time();
        }
        return 2 * (1 - phase) * std::sin(phase * 4 * std::numbers::pi_v<float>);
    }

    void update_popup_exit(const Theme& theme, bool animate) {
        const double now = motion.time();
        if (popups.size() < prior_popup_count)
            popup_retired = now;
        prior_popup_count = popups.size();
        if (!animate || now >= popup_branch_retired + theme.motion.popup)
            exiting_popups.clear();
        const auto first_change = std::ranges::mismatch(popups, drawn_popups);
        if (animate && first_change.in1 != popups.end() && first_change.in2 != drawn_popups.end()) {
            exiting_popup_start = static_cast<std::size_t>(first_change.in2 - drawn_popups.begin());
            exiting_popups.swap(drawn_popups);
            popup_branch_retired = now;
            drawn_popups = popups;
        } else if (!animate || popups.size() > drawn_popups.size() ||
            now >= std::max(popup_retired, hold_until + 3 * theme.motion.stagger) + theme.motion.popup) {
            if (drawn_popups != popups)
                drawn_popups = popups;
        }
    }

    void draw_exiting_layers(ImDrawList& draw, const Theme& theme, const Typography& type, PixelGrid grid,
        ViewSettings settings, const Mark* marking, Point pointer, float rounding) {
        OverlayAnimation exiting{.motion = motion, .rounding = rounding};
        if (!exiting_popups.empty()) {
            exiting_view.begin(layout(), settings, type);
            build_popups(exiting_view, exiting_popups);
            exiting_view.draw(draw, theme, atlas, grid, exiting, pointer, false, 0, false, false,
                nullptr, nullptr, {}, {}, {}, OverlayLayer::Popups, exiting_popup_start);
        }
        if (fading_mark && (!marking ||
                (&*fading_mark != marking && mark_scope(*fading_mark) != mark_scope(*marking)))) {
            exiting_view.begin(layout(), settings, type);
            const auto old_choices = mark_choices(*fading_mark);
            build_mark(exiting_view, *fading_mark, old_choices);
            exiting.marking_scope = mark_scope(*fading_mark);
            exiting.first_mark = fading_mark->first;
            exiting_view.draw(draw, theme, atlas, grid, exiting, pointer, false, 0, false, false,
                nullptr, &old_choices, {}, fading_mark->gesture.pivot(), {}, OverlayLayer::Marking);
        }
    }

    void draw_overlay(ImDrawList& draw, const Theme& theme, const Typography& type, PixelGrid grid,
        ViewSettings settings, std::array<Control, 8>& choices, bool was_visible, bool prepared,
        Point pointer, bool pressed, float rounding) {
        const double now = motion.time();
        const bool live = is_open();
        const bool animate = motion.enabled() && !cut_close;
        const bool hold = animate && now < hold_until;
        const bool hold_last = animate && now < hold_until + 3 * theme.motion.stagger;
        if (hold || (hold_last && !invoked.empty()))
            motion.keep_rendering();
        const float shake = shake_offset(theme);
        if (!animate || (fading_mark && shaken.empty() &&
                now >= std::max(mark_retired, hold_until + 3 * theme.motion.stagger) + theme.motion.popup))
            fading_mark.reset();
        const bool visible = mark && mark->gesture.visible();
        const bool menu_present = !popups.empty() || (hold && !drawn_popups.empty());
        auto* marking = visible ? &*mark : fading_mark && !menu_present ? &*fading_mark : nullptr;
        const bool ahead = mark && mark->gesture.ahead();
        if (visible && !marking->revealed) {
            marking->revealed = now;
            marking->first = Sector(pointer - marking->gesture.pivot(), 0).value_or(Dir::N);
        }
        const bool show_rows = (live || hold) && !ahead && !visible && !(hold && marking);
        if (show_rows && !rows_visible)
            rows_opened = now;
        rows_visible = show_rows;

        // Keep paths by ID while they exit. Resolve registry references each frame.
        update_popup_exit(theme, animate);
        const bool overflow = marking && has_mark_overflow(*marking);
        const auto& paint_popups = overflow ? popups : drawn_popups;
        std::size_t popup_count = paint_popups.size();
        if (!prepared || view_changed || was_visible != visible || marking != (visible ? &*mark : nullptr) ||
            paint_popups != popups || trigger.pinned() != settings.pinned) {
            settings.rows = config.show_menu_rows;
            settings.ticks = config.show_zone_ticks;
            settings.pinned = trigger.pinned();
            view.begin(layout(), settings, type);
            popup_count = build_popups(view, paint_popups);
            if (marking) {
                if (!visible)
                    choices = mark_choices(*marking);
                build_mark(view, *marking, choices);
            }
        }
        const auto* active_title = paint_popups.empty() ? nullptr
            : view.anchor(paint_popups[0].kind, paint_popups[0].id, 0);
        const bool overflow_live = overflow && (visible || (shake_in_popup && !shaken.empty()));
        const std::size_t active_count = hold_last && !invoked.empty()
            ? popup_count + static_cast<std::size_t>(overflow)
            : popups.size() + static_cast<std::size_t>(overflow_live);
        const auto scope = marking ? mark_scope(*marking) : 0;
        draw_exiting_layers(draw, theme, type, grid, settings, marking, pointer, rounding);
        view.draw(draw, theme, atlas, grid,
            {motion, rounding, now - rows_opened, marking ? now - marking->revealed.value_or(now) : 0,
                scope, marking ? marking->first : Dir::N, active_count, visible, live, cut_close || ahead,
                invoked, hold, hold_last, shaken, shake},
            pointer, live && pressed, config.scrim_opacity, (live || hold) && !ahead,
            show_rows, active_title, marking ? &choices : nullptr,
            marking ? marking->gesture.armed() : std::nullopt,
            marking ? marking->gesture.pivot() : Point{},
            ahead ? mark->gesture.ink() : std::span<const Point>{});
        if (!animate || (!fading_mark && drawn_popups == popups && !hold_last))
            invoked.clear();
        visual_tail = live || (animate && motion.animating());
    }

    void draw_toast(ImDrawList& draw, const Theme& theme, const Typography& type, PixelGrid grid,
        Rect work, float scale, float rounding) {
        if (!toast)
            return;
        const bool showing = ImGui::GetTime() < toast->expires;
        if (showing)
            motion.keep_rendering();
        const auto owner = motion.owner("confirmation");
        const float alpha = motion.tween(owner, "opacity", showing ? 1.0f : 0.0f,
            theme.motion.popup * (showing ? 1.0f : 0.7f),
            showing ? iam_ease_out_cubic : iam_ease_in_cubic);
        if (!showing && alpha <= 0.001f) {
            toast.reset();
            return;
        }
        if (alpha > 0.001f) {
            atlas.prepare_feedback(toast->command->desc.icon);
            OverlayView::toast(draw, theme, atlas, grid, type, work, scale, rounding,
                toast->pointer, toast->command->desc, alpha);
        }
    }

    void frame(Hotbox& widget) {
        attach();
        refresh_appearance();
        motion.begin();
        pending_frame = false;
        auto& registry_data = RegistryAccess::get(registry);
        RegistryFrame read(registry_data, context, ImGui::GetFrameCount());
        auto& io = ImGui::GetIO();
        const float dt = io.DeltaTime * 1000;
        const bool pointer_valid = ImGui::IsMousePosValid();
        const bool was_open = is_open();
        const auto pointer = pointer_valid ? PointOf(io.MousePos) : trigger.origin();
        const auto key = static_cast<ImGuiKey>(config.trigger & ~ImGuiMod_Mask_);
        const bool active_text = TextInputActive();
        const bool pressed = config.trigger != ImGuiKey_Space && ImGui::IsNamedKey(key) &&
            !active_text && ImGui::Shortcut(config.trigger, ImGuiInputFlags_RouteGlobal);
        const bool starting = pressed && trigger.phase() == Phase::Closed && can_open();
        const bool held = ImGui::IsNamedKey(key) && ImGui::IsKeyDown(key);
        const bool arming = trigger.phase() == Phase::Arming;
        const bool cancel = trigger.phase() != Phase::Closed &&
            (io.AppFocusLost || (arming && active_text) || ImGui::IsKeyPressed(ImGuiKey_Escape, false));
        const bool toggle_pin = was_open && held && ImGui::IsKeyPressed(ImGuiKey_P, false);
        if (starting)
            viewport_id = ViewportAt(pointer)->ID;
        auto transition = trigger.step({dt, pointer, pointer_valid, starting, held, cancel, toggle_pin},
            config.hold_threshold_ms);
        if (arming && transition == Transition::Opened && !can_open()) {
            trigger.close();
            transition = Transition::None;
        }
        signal(transition);
        if (trigger.phase() != Phase::Closed || was_open)
            Capture(io);

        const bool overlay_active = is_open() || was_open || visual_tail;
        auto* viewport = ViewportAt(overlay_active || toast ? trigger.origin() : pointer);
#ifdef IMGUI_HAS_VIEWPORT
        if (overlay_active || toast) {
            if (auto* captured_viewport = ImGui::FindViewportByID(viewport_id))
                viewport = captured_viewport;
        }
        const float viewport_scale = viewport->DpiScale;
        auto density = PointOf(viewport->FramebufferScale.x > 0
            ? viewport->FramebufferScale : io.DisplayFramebufferScale);
#else
        constexpr float viewport_scale = 1.0f;
        auto density = PointOf(io.DisplayFramebufferScale);
#endif
        density.x = density.x > 0 ? density.x : 1;
        density.y = density.y > 0 ? density.y : 1;
        const PixelGrid grid{PointOf(viewport->Pos), density};
        const float scale = std::max(viewport_scale * ImGui::GetStyle().FontScaleMain, 0.5f);
        float rounding;
        const auto theme = motion.palette(resolved_theme(), rounding);
        const bool warmed = atlas.prepare(*io.Fonts, registry_data.current, scale, density.x,
            (io.BackendFlags & ImGuiBackendFlags_RendererHasTextures) != 0);
        if (warmed || overlay_active || toast) {
            const Typography type(theme, scale, density.x);
            if (warmed)
                type.warm(registry_data.current);
            const auto work_pos = PointOf(viewport->WorkPos);
            const auto work_size = PointOf(viewport->WorkSize);
            const ViewSettings settings{trigger.origin(), {work_pos, work_pos + work_size}, scale,
                config.show_menu_rows, config.show_zone_ticks, trigger.pinned(), config.monogram_fallback};
            auto choices = mark ? mark_choices(*mark) : std::array<Control, 8>{};
            const bool was_visible = mark && mark->gesture.visible();
            const bool prepared = (is_open() || was_open) && !cancel;
            if (prepared) {
                view.begin(layout(), settings, type);
                popups.resize(build_popups(view, popups));
                if (was_visible)
                    build_mark(view, *mark, choices);
            }
            view_changed = false;
            const bool can_resolve = prepared && pointer_valid &&
                (is_open() || (mark && io.MouseReleased[mark->button]));
            if (mark && can_resolve) {
                mark_step(pointer, dt, scale, io, choices);
            } else if (!mark && is_open() && pointer_valid) {
                idle_input(pointer, io, scale);
            } else if (prepared && !mark && was_open && pointer_valid && transition == Transition::Closed && !held) {
                if (const auto* hit = view.popup_item(pointer); hit && hit->kind == ControlKind::Command)
                    enqueue(*hit, InvokeSource::MenuRow, ImGuiMouseButton_Left, io.KeyShift);
            }

            if (!is_open())
                clear_session();
#ifdef IMGUI_HAS_VIEWPORT
            auto* foreground = ImGui::GetForegroundDrawList(viewport);
#else
            auto* foreground = ImGui::GetForegroundDrawList();
#endif
            if (overlay_active)
                draw_overlay(*foreground, theme, type, grid, settings, choices, was_visible, prepared,
                    pointer, io.MouseDown[ImGuiMouseButton_Left], rounding);
            draw_toast(*foreground, theme, type, grid, settings.work, scale, rounding);
        }
        cut_close = false;
        dispatch(widget);
    }
};

Hotbox::Hotbox(Registry& registry, Config config) : impl_(std::make_unique<Impl>(registry, std::move(config))) {}
Hotbox::~Hotbox() = default;

void Hotbox::frame() { impl_->frame(*this); }

void Hotbox::open(ImVec2 screen_pos) {
    if ((ImGui::GetCurrentContext() && TextInputActive()) || !impl_->can_open())
        return;
    impl_->clear_session();
    impl_->pending_frame = true;
    impl_->hold_until = 0;
    impl_->invoked.clear();
    const auto point = PointOf(screen_pos);
    if (ImGui::GetCurrentContext())
        impl_->viewport_id = ViewportAt(point)->ID;
    impl_->signal(impl_->trigger.open(point));
}

void Hotbox::close(bool animated) {
    impl_->close(animated);
}

bool Hotbox::is_open() const { return impl_->is_open(); }
void Hotbox::set_pinned(bool pinned) { impl_->trigger.set_pinned(pinned); }
void Hotbox::set_motion(MotionPrefs prefs) {
    impl_->motion_explicit = true;
    impl_->motion.set(prefs);
    impl_->pending_frame = true;
}

bool Hotbox::is_animating() const { return impl_->pending_frame || impl_->motion.animating(); }
MotionPrefs Hotbox::motion_prefs() const { return impl_->motion.prefs(); }

void Hotbox::set_theme(const Theme& light, const Theme& dark) {
    impl_->light = light;
    impl_->dark = dark;
    impl_->pending_frame = true;
}

void Hotbox::set_theme_mode(ThemeMode mode) {
    impl_->theme_mode = mode;
    if (mode == ThemeMode::System && !impl_->motion_explicit)
        impl_->motion.set({1, SystemReduceMotion()});
    impl_->refresh_appearance();
    impl_->pending_frame = true;
}

void Hotbox::set_system_appearance_provider(std::function<ThemeMode()> provider) {
    impl_->appearance_provider = std::move(provider);
    impl_->refresh_appearance();
    impl_->pending_frame = true;
}

const Theme& Hotbox::current_theme() const { return impl_->resolved_theme(); }

} // namespace hotbox

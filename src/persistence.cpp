#include "persistence.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <utility>
#include <vector>

namespace hotbox::detail {
namespace {

constexpr const char* type_name = "Hotbox";

struct Attached {
    ImGuiContext* context;
    SettingsClient* client;
};

// Main-thread only, like the rest of the library.
std::vector<Attached>& Clients() {
    static std::vector<Attached> clients;
    return clients;
}

SettingsClient* Find(ImGuiContext* context, std::string_view id) {
    for (const auto& entry : Clients()) {
        if (entry.context == context && entry.client->settings_id() == id)
            return entry.client;
    }
    return nullptr;
}

void* ReadOpen(ImGuiContext* context, ImGuiSettingsHandler*, const char* name) {
    auto* client = Find(context, name);
    if (client)
        client->begin_settings();
    return client;
}

void ReadLine(ImGuiContext*, ImGuiSettingsHandler*, void* entry, const char* line) {
    static_cast<SettingsClient*>(entry)->read_setting(line);
}

void WriteAll(ImGuiContext* context, ImGuiSettingsHandler*, ImGuiTextBuffer* out) {
    std::string lines;
    for (const auto& entry : Clients()) {
        if (entry.context != context)
            continue;
        lines.clear();
        entry.client->write_settings(lines);
        const auto id = entry.client->settings_id();
        out->appendf("[%s][%.*s]\n", type_name, static_cast<int>(id.size()), id.data());
        out->append(lines.data(), lines.data() + lines.size());
        out->append("\n");
    }
}

/// Read the client's section from an ini file that was loaded before the client attached.
void ReadLateSection(const char* filename, SettingsClient& client) {
    std::size_t size = 0;
    char* data = static_cast<char*>(ImFileLoadToMemory(filename, "rb", &size));
    if (!data)
        return;
    const std::string_view text(data, size);
    const auto id = client.settings_id();
    bool inside = false;
    for (std::size_t start = 0; start < text.size();) {
        auto end = text.find('\n', start);
        end = end == std::string_view::npos ? text.size() : end;
        auto line = text.substr(start, end - start);
        start = end + 1;
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        if (line.starts_with('[')) {
            const std::string_view prefix = "[Hotbox][";
            inside = line.starts_with(prefix) && line.ends_with(']') &&
                line.substr(prefix.size(), line.size() - prefix.size() - 1) == id;
            if (inside)
                client.begin_settings();
        } else if (inside && !line.empty()) {
            client.read_setting(line);
        }
    }
    IM_FREE(data);
}

} // namespace

void AttachSettings(ImGuiContext& context, SettingsClient& client) {
    if (client.settings_id().empty())
        return;
    auto& clients = Clients();
    if (std::ranges::any_of(clients, [&](const auto& entry) { return entry.client == &client; }))
        return;
    clients.push_back({&context, &client});
    ImGuiContext* previous = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(&context);
    if (!ImGui::FindSettingsHandler(type_name)) {
        ImGuiSettingsHandler handler;
        handler.TypeName = type_name;
        handler.TypeHash = ImHashStr(type_name);
        handler.ReadOpenFn = ReadOpen;
        handler.ReadLineFn = ReadLine;
        handler.WriteAllFn = WriteAll;
        ImGui::AddSettingsHandler(&handler);
    }
    ImGui::SetCurrentContext(previous);
    if (context.SettingsLoaded && context.IO.IniFilename)
        ReadLateSection(context.IO.IniFilename, client);
}

void DetachSettings(SettingsClient& client, bool context_alive) {
    auto& clients = Clients();
    const auto match = std::ranges::find(clients, &client, &Attached::client);
    if (match == clients.end())
        return;
    ImGuiContext* context = match->context;
    clients.erase(match);
    if (!context_alive)
        return;
    if (std::ranges::any_of(clients, [&](const auto& entry) { return entry.context == context; }))
        return;
    ImGuiContext* previous = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(context);
    if (ImGui::FindSettingsHandler(type_name))
        ImGui::RemoveSettingsHandler(type_name);
    ImGui::SetCurrentContext(previous);
}

} // namespace hotbox::detail

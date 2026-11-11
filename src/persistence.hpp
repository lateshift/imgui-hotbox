#pragma once

#include <string>
#include <string_view>

struct ImGuiContext;

namespace hotbox::detail {

/// A hotbox whose state is stored in the host's imgui.ini as `[Hotbox][<settings id>]`.
class SettingsClient {
public:
    virtual ~SettingsClient() = default;
    virtual std::string_view settings_id() const = 0;
    /// The section is about to be read: drop the current state.
    virtual void begin_settings() = 0;
    virtual void read_setting(std::string_view line) = 0;
    /// Append the section's lines, without the header.
    virtual void write_settings(std::string& out) const = 0;
};

/// Register the client with the context's settings. If the context has already loaded its
/// ini file, the client's section is read from that file now.
void AttachSettings(ImGuiContext& context, SettingsClient& client);
/// Remove the client. Pass `context_alive = false` after the context has shut down.
void DetachSettings(SettingsClient& client, bool context_alive);

} // namespace hotbox::detail

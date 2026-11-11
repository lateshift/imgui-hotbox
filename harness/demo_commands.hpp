#pragma once

#include <hotbox/registry.hpp>

namespace harness {

struct DemoCommands {
    hotbox::Registry registry;
    std::string last_command;
    bool snap = true;

    /// `private_icons` selects the in-house icon set when the build has it.
    explicit DemoCommands(bool private_icons = true);
};

} // namespace harness

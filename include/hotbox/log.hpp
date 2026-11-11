#pragma once

#include <memory>

namespace spdlog { class logger; }

namespace hotbox {

/// Replace the library logger. An empty pointer restores the default "hotbox" logger.
void set_logger(std::shared_ptr<spdlog::logger> logger);

} // namespace hotbox

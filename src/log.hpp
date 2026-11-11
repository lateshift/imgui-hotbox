#pragma once

#ifdef HOTBOX_WITH_SPDLOG
#include <spdlog/logger.h>
#include <memory>

namespace hotbox::detail {
const std::shared_ptr<spdlog::logger>& logger();
} // namespace hotbox::detail
#endif

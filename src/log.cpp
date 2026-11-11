#include <hotbox/log.hpp>

#ifdef HOTBOX_WITH_SPDLOG
#include "log.hpp"

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <utility>

namespace hotbox {
namespace {
std::shared_ptr<spdlog::logger> override_logger;
}

const std::shared_ptr<spdlog::logger>& detail::logger() {
    if (override_logger)
        return override_logger;
    static const auto default_logger = [] {
        auto existing = spdlog::get("hotbox");
        return existing ? existing : spdlog::stdout_color_mt("hotbox");
    }();
    return default_logger;
}

void set_logger(std::shared_ptr<spdlog::logger> logger) {
    override_logger = std::move(logger);
}
} // namespace hotbox

#else

void hotbox::set_logger(std::shared_ptr<spdlog::logger>) {}

#endif

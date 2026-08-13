#include "nds/logging.hh"

#include <mutex>
#include <stdexcept>
#include <syslog.h>
#include <unordered_map>

#include <spdlog/sinks/null_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/syslog_sink.h>

namespace nds::log {
namespace {

std::mutex registry_mutex;
std::unordered_map<std::string, std::shared_ptr<spdlog::logger>> registry;

std::shared_ptr<spdlog::logger> null_logger(std::string_view name) {
    auto sink = std::make_shared<spdlog::sinks::null_sink_mt>();
    auto value = std::make_shared<spdlog::logger>(std::string(name), sink);
    value->set_level(spdlog::level::off);
    return value;
}

bool parse_level(std::string_view name, spdlog::level::level_enum *level) {
    if (level == nullptr)
        return false;
    if (name == "trace")
        *level = spdlog::level::trace;
    else if (name == "debug")
        *level = spdlog::level::debug;
    else if (name == "info")
        *level = spdlog::level::info;
    else if (name == "warn")
        *level = spdlog::level::warn;
    else if (name == "error")
        *level = spdlog::level::err;
    else if (name == "critical")
        *level = spdlog::level::critical;
    else if (name == "off")
        *level = spdlog::level::off;
    else
        return false;
    return true;
}

}  // namespace

void set_logger(std::string_view name, std::shared_ptr<spdlog::logger> value) {
    std::lock_guard lock(registry_mutex);
    registry[std::string(name)] = value == nullptr ? null_logger(name) : std::move(value);
}

std::shared_ptr<spdlog::logger> logger(std::string_view name) {
    std::lock_guard lock(registry_mutex);
    const auto key = std::string(name);
    const auto found = registry.find(key);
    if (found != registry.end())
        return found->second;
    auto value = null_logger(name);
    registry.emplace(key, value);
    return value;
}

Result<void> configure(std::string_view name, std::string_view sink_name, std::string_view level_name) {
    try {
        std::shared_ptr<spdlog::sinks::sink> sink;
        spdlog::level::level_enum level;
        if (sink_name == "stderr")
            sink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
        else if (sink_name == "stdout")
            sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        else if (sink_name == "syslog")
            sink = std::make_shared<spdlog::sinks::syslog_sink_mt>(std::string(name), 0, LOG_USER, true);
        else if (sink_name == "none")
            sink = std::make_shared<spdlog::sinks::null_sink_mt>();
        else {
            return unexpected(ErrorCode::kInvalidArgument, "unsupported log sink: " + std::string(sink_name));
        }
        if (!parse_level(level_name, &level)) {
            return unexpected(ErrorCode::kInvalidArgument, "unsupported log level: " + std::string(level_name));
        }
        auto value = std::make_shared<spdlog::logger>(std::string(name), sink);
        value->set_level(level);
        value->set_pattern("%Y-%m-%dT%H:%M:%S.%e%z [%n] [%^%l%$] %v");
        set_logger(name, std::move(value));
        return {};
    } catch (const std::exception &exception) {
        return unexpected(ErrorCode::kRuntime, exception.what());
    }
}

}  // namespace nds::log

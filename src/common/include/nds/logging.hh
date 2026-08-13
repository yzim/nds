#ifndef NDS_LOGGING_HPP
#define NDS_LOGGING_HPP

#include "nds/result.hh"

#include <memory>
#include <string>
#include <string_view>

#include <spdlog/logger.h>
#include <spdlog/spdlog.h>

namespace nds::log {

/*
 * NDS uses named spdlog loggers. Applications embedding an NDS component can
 * install any spdlog logger/sink before it does work; the CLI tools configure
 * their logger from --log-sink and --log-level.
 */
void set_logger(std::string_view name, std::shared_ptr<spdlog::logger> logger);
std::shared_ptr<spdlog::logger> logger(std::string_view name);
Result<void> configure(std::string_view name, std::string_view sink, std::string_view level);

}  // namespace nds::log

#define NDS_LOG_TRACE(name, ...) SPDLOG_LOGGER_TRACE(::nds::log::logger(name), __VA_ARGS__)
#define NDS_LOG_DEBUG(name, ...) SPDLOG_LOGGER_DEBUG(::nds::log::logger(name), __VA_ARGS__)
#define NDS_LOG_INFO(name, ...) SPDLOG_LOGGER_INFO(::nds::log::logger(name), __VA_ARGS__)
#define NDS_LOG_WARN(name, ...) SPDLOG_LOGGER_WARN(::nds::log::logger(name), __VA_ARGS__)
#define NDS_LOG_ERROR(name, ...) SPDLOG_LOGGER_ERROR(::nds::log::logger(name), __VA_ARGS__)

#endif

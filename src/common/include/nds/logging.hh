#ifndef NDS_LOGGING_HPP
#define NDS_LOGGING_HPP

#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include <spdlog/logger.h>
#include <spdlog/spdlog.h>

#include <fmt/printf.h>

namespace nds::log {

/*
 * NDS uses named spdlog loggers. Applications embedding an NDS component can
 * install any spdlog logger/sink before it does work; the CLI tools configure
 * their logger from --log-sink and --log-level.
 */
void set_logger(std::string_view name, std::shared_ptr<spdlog::logger> logger);
std::shared_ptr<spdlog::logger> logger(std::string_view name);
bool configure(std::string_view name, std::string_view sink, std::string_view level,
               std::string &error);

template <typename... Arguments>
void log_printf(std::string_view name, spdlog::level::level_enum level, const char *format,
                Arguments &&...arguments)
{
    std::string message = fmt::sprintf(format, std::forward<Arguments>(arguments)...);
    while (!message.empty() && (message.back() == '\n' || message.back() == '\r')) message.pop_back();
    logger(name)->log(level, "{}", message);
}

class Line {
public:
    Line(std::string_view name, spdlog::level::level_enum level);
    ~Line();
    Line(const Line &) = delete;
    Line &operator=(const Line &) = delete;
    Line(Line &&) = delete;
    Line &operator=(Line &&) = delete;

    template <typename Value>
    Line &operator<<(Value &&value)
    {
        stream_ << std::forward<Value>(value);
        return *this;
    }

private:
    std::shared_ptr<spdlog::logger> logger_;
    spdlog::level::level_enum level_;
    std::ostringstream stream_;
};

} // namespace nds::log

#define NDS_LOG_TRACE(name, ...) SPDLOG_LOGGER_TRACE(::nds::log::logger(name), __VA_ARGS__)
#define NDS_LOG_DEBUG(name, ...) SPDLOG_LOGGER_DEBUG(::nds::log::logger(name), __VA_ARGS__)
#define NDS_LOG_INFO(name, ...) SPDLOG_LOGGER_INFO(::nds::log::logger(name), __VA_ARGS__)
#define NDS_LOG_WARN(name, ...) SPDLOG_LOGGER_WARN(::nds::log::logger(name), __VA_ARGS__)
#define NDS_LOG_ERROR(name, ...) SPDLOG_LOGGER_ERROR(::nds::log::logger(name), __VA_ARGS__)
#define NDS_LOG_INFOF(name, ...) ::nds::log::log_printf(name, spdlog::level::info, __VA_ARGS__)
#define NDS_LOG_ERRORF(name, ...) ::nds::log::log_printf(name, spdlog::level::err, __VA_ARGS__)
#define NDS_LOG_INFO_LINE(name) ::nds::log::Line(name, spdlog::level::info)
#define NDS_LOG_ERROR_LINE(name) ::nds::log::Line(name, spdlog::level::err)

#endif

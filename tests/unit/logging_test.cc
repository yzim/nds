#include "logging.hh"

#include <gtest/gtest.h>
#include <memory>
#include <sstream>
#include <string>

#include <spdlog/sinks/ostream_sink.h>

TEST(LoggingTest, UsesInjectedSinkAndRejectsInvalidConfiguration) {
    std::ostringstream output;
    auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(output);
    auto external = std::make_shared<spdlog::logger>("test", sink);
    external->set_level(spdlog::level::trace);
    external->set_pattern("%v");
    nds::log::set_logger("test", external);

    NDS_LOG_INFO("test", "message {}", 7);
    NDS_LOG_ERROR("test", "failure {}", 8);
    NDS_LOG_INFO("test", "formatted {}", "message");
    external->flush();
    EXPECT_TRUE(output.str() == "message 7\nfailure 8\nformatted message\n");

    const auto invalid_sink = nds::log::configure("test", "invalid", "info");
    EXPECT_TRUE(!invalid_sink);
    EXPECT_TRUE(invalid_sink.error().message == "unsupported log sink: invalid");
    const auto invalid_level = nds::log::configure("test", "none", "invalid");
    EXPECT_TRUE(!invalid_level);
    EXPECT_TRUE(invalid_level.error().message == "unsupported log level: invalid");
}

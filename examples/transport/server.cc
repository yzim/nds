#include "nds/logging.hh"
#include "transport.hh"

#include <CLI/CLI.hpp>

#include <array>
#include <cstddef>
#include <cstdlib>
#include <string>

namespace {

struct Config {
    nds::server::ConnectionConfig connection;
    std::string log_sink{"stderr"};
    std::string log_level{"info"};
};

int parse(int argc, char **argv, Config *config, bool *exit_requested) {
    CLI::App app{"Receive one NDS transport Send."};
    app.add_option("--device", config->connection.backend.device_name)->required();
    app.add_option("--gid-index", config->connection.backend.gid_index)->required();
    app.add_option("--listen", config->connection.listen_address);
    app.add_option("--tcp-port", config->connection.tcp_port);
    app.add_option("--ib-port", config->connection.backend.port);
    app.add_option("--log-sink", config->log_sink)->check(CLI::IsMember({"stderr", "stdout", "syslog", "none"}));
    app.add_option("--log-level", config->log_level)
        ->check(CLI::IsMember({"trace", "debug", "info", "warn", "error", "critical", "off"}));
    try {
        app.parse(argc, argv);
    } catch (const CLI::CallForHelp &help) {
        *exit_requested = true;
        return app.exit(help);
    } catch (const CLI::ParseError &error) {
        return app.exit(error);
    }
    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    (void)nds::log::configure("transport-server", "stderr", "info");
    Config config;
    bool exit_requested{};
    const int result = parse(argc, argv, &config, &exit_requested);
    if (exit_requested || result != 0)
        return result;
    if (!nds::log::configure("transport-server", config.log_sink, config.log_level))
        return EXIT_FAILURE;

    nds::server::Connection connection;
    if (const auto opened = connection.open(config.connection); !opened) {
        NDS_LOG_ERROR("transport-server", "connection open failed: {}", opened.error().message);
        return EXIT_FAILURE;
    }
    std::array<std::byte, 64U> payload{};
    const auto prepared = connection.prepare_receive(payload.data(), payload.size());
    if (!prepared || !connection.activate() || !connection.receive(5000U)) {
        NDS_LOG_ERROR("transport-server", "transport receive failed");
        return EXIT_FAILURE;
    }
    NDS_LOG_INFO("transport-server", "completed one NDS transport receive");
    return EXIT_SUCCESS;
}

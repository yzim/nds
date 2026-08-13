#include "nds/logging.hh"
#include "protocol.hh"
#include "transport.hh"

#include <CLI/CLI.hpp>

#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace {
struct Config {
    nds::server::ConnectionConfig connection;
    std::uint32_t namespace_bytes{1024U * 1024U};
    std::string log_sink{"stderr"};
    std::string log_level{"info"};
};

int parse(int argc, char **argv, Config *config, bool *exit_requested) {
    CLI::App app{"Serve one-command NDS memory-backed storage requests."};
    app.add_option("--device", config->connection.backend.device_name)->required();
    app.add_option("--gid-index", config->connection.backend.gid_index)->required();
    app.add_option("--listen", config->connection.listen_address);
    app.add_option("--tcp-port", config->connection.tcp_port);
    app.add_option("--ib-port", config->connection.backend.port);
    app.add_option("--namespace-bytes", config->namespace_bytes)->check(CLI::Range(1U, 64U * 1024U * 1024U));
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
    (void)nds::log::configure("cpu-server", "stderr", "info");
    Config config;
    bool exit_requested{};
    const int result = parse(argc, argv, &config, &exit_requested);
    if (exit_requested || result != 0)
        return result;
    if (!nds::log::configure("cpu-server", config.log_sink, config.log_level))
        return EXIT_FAILURE;
    nds::server::Connection connection;
    if (const auto opened = connection.open(config.connection); !opened) {
        NDS_LOG_ERROR("cpu-server", "server connection failed: {}", opened.error().message);
        return EXIT_FAILURE;
    }
    std::vector<unsigned char> storage(config.namespace_bytes, 0U);
    if (const auto served = nds::server::serve_request(&connection, &storage, 5000U); !served) {
        NDS_LOG_ERROR("cpu-server", "protocol request failed: {}", served.error().message);
        return EXIT_FAILURE;
    }
    NDS_LOG_INFO("cpu-server", "completed one NDS storage command");
    return EXIT_SUCCESS;
}

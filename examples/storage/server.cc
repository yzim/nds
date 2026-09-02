#include "logging.hh"
#include "storage.hh"
#include "transport.hh"

#include <CLI/CLI.hpp>

#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace {
struct Config {
    nds::server::TransportConfig transport;
    std::uint32_t clients{1U};
    std::uint32_t namespace_bytes{1024U * 1024U};
    std::uint32_t storage_commands{1U};
    bool seed_pattern{};
    std::uint32_t verify_write_bytes{};
    std::string log_sink{"stderr"};
    std::string log_level{"info"};
};

int parse(int argc, char **argv, Config *config, bool *exit_requested) {
    CLI::App app{"Serve NDS memory-backed storage commands."};
    app.add_option("--device", config->transport.endpoint.device_name)->required();
    app.add_option("--gid-index", config->transport.endpoint.gid_index)->required();
    app.add_option("--listen", config->transport.listen_address, "TCP exchange listen address as IPv4:port");
    app.add_option("--ib-port", config->transport.endpoint.port);
    app.add_option("--max-qp-count", config->transport.max_qp_count, "Maximum QPs accepted per client")
        ->check(CLI::Range(1U, nds::wire::kMaxQpInfoBatch));
    app.add_option("--clients", config->clients, "Number of serial client sessions to serve")
        ->check(CLI::Range(1U, 65535U));
    app.add_option("--namespace-bytes", config->namespace_bytes)->check(CLI::Range(1U, 64U * 1024U * 1024U));
    app.add_option("--storage-commands", config->storage_commands,
                   "Number of storage commands to serve on one transport")
        ->check(CLI::Range(1U, 65535U));
    app.add_flag("--seed-pattern", config->seed_pattern, "Initialize the namespace with the NDS test pattern");
    app.add_option("--verify-write-bytes", config->verify_write_bytes,
                   "Verify this many leading namespace bytes against the NDS test pattern")
        ->check(CLI::Range(0U, 64U * 1024U * 1024U));
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
    if (!nds::log::configure("cpu-server", config.log_sink, config.log_level).ok())
        return EXIT_FAILURE;
    nds::server::TransportListener listener;
    if (const auto opened = listener.open(config.transport); !opened.ok()) {
        NDS_LOG_ERROR("cpu-server", "server listener failed: {}", opened.error().message);
        return EXIT_FAILURE;
    }
    std::vector<std::uint8_t> storage(config.namespace_bytes, 0U);
    if (config.seed_pattern) {
        for (std::size_t index = 0; index < storage.size(); ++index)
            storage[index] = static_cast<std::uint8_t>(index ^ 0x5aU);
    }
    for (std::uint32_t client_index = 0U; client_index < config.clients; ++client_index) {
        nds::server::Transport transport;
        if (const auto accepted = listener.accept(&transport); !accepted.ok()) {
            NDS_LOG_ERROR("cpu-server", "client {} transport failed: {}", client_index, accepted.error().message);
            return EXIT_FAILURE;
        }
        if (const auto served = nds::server::serve_commands(&transport, &storage, config.storage_commands,
                                                             config.transport.completion_timeout_ms);
            !served.ok()) {
            NDS_LOG_ERROR("cpu-server", "client {} protocol command failed: {}", client_index, served.error().message);
            return EXIT_FAILURE;
        }
    }
    if (config.verify_write_bytes > storage.size()) {
        NDS_LOG_ERROR("cpu-server", "write verification range exceeds the namespace");
        return EXIT_FAILURE;
    }
    for (std::size_t index = 0; index < config.verify_write_bytes; ++index) {
        if (storage[index] != static_cast<std::uint8_t>(index ^ 0x5aU)) {
            NDS_LOG_ERROR("cpu-server", "storage Write verification failed at byte {}", index);
            return EXIT_FAILURE;
        }
    }
    NDS_LOG_INFO("cpu-server", "completed {} NDS storage sessions", config.clients);
    return EXIT_SUCCESS;
}

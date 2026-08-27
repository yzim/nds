#include "nds/logging.hh"
#include "transport.hh"

#include <CLI/CLI.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

struct Config {
    nds::server::TransportConfig transport;
    std::uint32_t receives{1U};
    std::string operation{"send"};
    std::string log_sink{"stderr"};
    std::string log_level{"info"};
};

int parse(int argc, char **argv, Config *config, bool *exit_requested) {
    CLI::App app{"Receive one or more NDS verbs Sends."};
    app.add_option("--device", config->transport.backend.device_name)->required();
    app.add_option("--gid-index", config->transport.backend.gid_index)->required();
    app.add_option("--listen", config->transport.listen_address);
    app.add_option("--ib-port", config->transport.backend.port);
    app.add_option("--qp-count", config->transport.qp_count, "Connected QPs to create")
        ->check(CLI::Range(1U, nds::wire::kMaxQpInfoBatch));
    app.add_option("--receives", config->receives, "Number of Receive WQEs to post")->check(CLI::Range(1U, 2U));
    app.add_option("--operation", config->operation)
        ->check(CLI::IsMember({"send", "send-batch", "send-batch-invalid", "recv"}));
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
    (void)nds::log::configure("verbs-server", "stderr", "info");
    Config config;
    bool exit_requested{};
    const int result = parse(argc, argv, &config, &exit_requested);
    if (exit_requested || result != 0)
        return result;
    if (!nds::log::configure("verbs-server", config.log_sink, config.log_level))
        return EXIT_FAILURE;

    nds::server::Transport transport;
    if (const auto opened = transport.open(config.transport); !opened) {
        NDS_LOG_ERROR("verbs-server", "transport open failed: {}", opened.error().message);
        return EXIT_FAILURE;
    }
    if (config.operation == "recv") {
        std::array<std::byte, 64U> payload{};
        for (std::size_t index = 0U; index < payload.size(); ++index)
            payload[index] = static_cast<std::byte>(index ^ 0x5aU);
        const auto region =
            transport.register_memory(payload.data(), payload.size(), nds::server::MemoryAccess::LocalRead);
        std::uint8_t ready{};
        if (!region || !transport.activate() || !transport.bootstrap()->receive_bytes(&ready, sizeof(ready)) ||
            ready != 1U || !transport.send(*region, payload.size())) {
            NDS_LOG_ERROR("verbs-server", "verbs PostRecv exchange failed");
            return EXIT_FAILURE;
        }
        NDS_LOG_INFO("verbs-server", "completed NDS verbs receive peer exchange");
        return EXIT_SUCCESS;
    }
    std::vector<std::array<std::byte, 64U>> payloads(config.receives);
    std::vector<nds::server::RegisteredRegion> receives;
    receives.reserve(payloads.size());
    for (auto &payload : payloads) {
        auto prepared = transport.prepare_receive(payload.data(), payload.size());
        if (!prepared) {
            NDS_LOG_ERROR("verbs-server", "verbs Receive setup failed");
            return EXIT_FAILURE;
        }
        receives.push_back(std::move(*prepared));
    }
    if (!transport.activate()) {
        NDS_LOG_ERROR("verbs-server", "verbs Receive activation failed");
        return EXIT_FAILURE;
    }
    for (std::uint32_t receive_index = 0U; receive_index < config.receives; ++receive_index) {
        if (!transport.receive(5000U)) {
            NDS_LOG_ERROR("verbs-server", "verbs Receive {} failed", receive_index);
            return EXIT_FAILURE;
        }
        for (std::size_t byte_index = 0U; byte_index < payloads[receive_index].size(); ++byte_index) {
            const std::byte expected =
                static_cast<std::byte>((receive_index * payloads[receive_index].size() + byte_index) ^ 0x5aU);
            if (payloads[receive_index][byte_index] != expected) {
                NDS_LOG_ERROR("verbs-server", "verbs Receive {} payload mismatch at byte {}", receive_index,
                              byte_index);
                return EXIT_FAILURE;
            }
        }
    }
    NDS_LOG_INFO("verbs-server", "completed {} NDS verbs receives", config.receives);
    return EXIT_SUCCESS;
}

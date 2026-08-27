#include "nds/logging.hh"
#include "nds/wire/transport.hh"
#include "transport.hh"

#include <CLI/CLI.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>

namespace {
constexpr std::size_t kBytes = 64U;
using Payload = std::array<std::byte, kBytes>;
enum class Operation { Send, Recv, Read, Write };

struct Config {
    nds::server::TransportConfig transport;
    Operation operation{Operation::Send};
    std::string log_sink{"stderr"};
    std::string log_level{"info"};
};

int parse(int argc, char **argv, Config *config, bool *exit_requested) {
    std::string operation{"send"};
    CLI::App app{"Peer one direct NDS transport operator."};
    app.add_option("--device", config->transport.backend.device_name)->required();
    app.add_option("--gid-index", config->transport.backend.gid_index)->required();
    app.add_option("--listen", config->transport.listen_address);
    app.add_option("--ib-port", config->transport.backend.port);
    app.add_option("--qp-count", config->transport.qp_count, "Connected QPs to create")
        ->check(CLI::Range(1U, nds::wire::kMaxQpInfoBatch));
    app.add_option("--operation", operation)->check(CLI::IsMember({"send", "recv", "read", "write"}));
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
    if (operation == "recv")
        config->operation = Operation::Recv;
    else if (operation == "read")
        config->operation = Operation::Read;
    else if (operation == "write")
        config->operation = Operation::Write;
    return 0;
}

Payload payload() {
    Payload value{};
    for (std::size_t index = 0U; index < value.size(); ++index) value[index] = static_cast<std::byte>(index ^ 0x5aU);
    return value;
}

bool wait_signal(nds::server::Transport *transport) {
    std::uint8_t value{};
    return transport->bootstrap()->receive_bytes(&value, sizeof(value)) && value == 1U;
}

bool publish_memory(nds::server::Transport *transport, const nds::server::RegisteredRegion &region) {
    nds::wire::RemoteMemory wire{};
    const nds::transport::RemoteMemory memory{reinterpret_cast<std::uint64_t>(region.address()),
                                              static_cast<std::uint32_t>(region.length()), region.remote_key()};
    return nds::transport::encode(&memory, &wire) == nds::transport::CodecResult::Ok &&
           transport->bootstrap()->send_bytes(&wire, sizeof(wire));
}

bool verify(const Payload &received) {
    return received == payload();
}
}  // namespace

int main(int argc, char **argv) {
    (void)nds::log::configure("transport-server", "stderr", "info");
    Config config;
    bool exit_requested{};
    const int parsed = parse(argc, argv, &config, &exit_requested);
    if (exit_requested || parsed != 0)
        return parsed;
    if (!nds::log::configure("transport-server", config.log_sink, config.log_level))
        return EXIT_FAILURE;
    nds::server::Transport transport;
    if (const auto opened = transport.open(config.transport); !opened) {
        NDS_LOG_ERROR("transport-server", "transport open failed: {}", opened.error().message);
        return EXIT_FAILURE;
    }
    Payload buffer{};
    if (config.operation == Operation::Send) {
        const auto region = transport.prepare_receive(buffer.data(), buffer.size());
        if (!region || !transport.activate() || !transport.receive(5000U) || !verify(buffer)) {
            NDS_LOG_ERROR("transport-server", "RdmaSend exchange failed");
            return EXIT_FAILURE;
        }
    } else if (config.operation == Operation::Recv) {
        buffer = payload();
        const auto region =
            transport.register_memory(buffer.data(), buffer.size(), nds::server::MemoryAccess::LocalRead);
        if (!region || !transport.activate() || !wait_signal(&transport) || !transport.send(*region, buffer.size())) {
            NDS_LOG_ERROR("transport-server", "RdmaRecv exchange failed");
            return EXIT_FAILURE;
        }
    } else if (config.operation == Operation::Read) {
        buffer = payload();
        const auto region =
            transport.register_memory(buffer.data(), buffer.size(), nds::server::MemoryAccess::RemoteRead);
        if (!region || !transport.activate() || !publish_memory(&transport, *region) || !wait_signal(&transport)) {
            NDS_LOG_ERROR("transport-server", "RdmaRead exchange failed");
            return EXIT_FAILURE;
        }
    } else {
        std::array<std::byte, 1U> completion{};
        const auto receive = transport.prepare_receive(completion.data(), completion.size());
        const auto region =
            transport.register_memory(buffer.data(), buffer.size(), nds::server::MemoryAccess::RemoteWrite);
        if (!receive || !region || !transport.activate() || !publish_memory(&transport, *region) ||
            !transport.receive(5000U) ||
            !verify(buffer)) {
            NDS_LOG_ERROR("transport-server", "RdmaWrite exchange failed");
            return EXIT_FAILURE;
        }
    }
    NDS_LOG_INFO("transport-server", "completed NDS transport operation");
    return EXIT_SUCCESS;
}

#include "verbs_wire.hh"

#include "logging.hh"
#include "transport_protocol.hh"
#include "transport.hh"

#include <CLI/CLI.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace {

constexpr std::uint32_t kMaximumInFlight = 16U;

struct Config {
    nds::server::TransportConfig transport;
    nds::benchmark::VerbsOperation operation{nds::benchmark::VerbsOperation::Read};
    std::uint32_t bytes{};
    std::uint32_t in_flight{1U};
};

nds::Result<Config> parse(int argc, char **argv) {
    Config config;
    std::string operation{"read"};
    CLI::App app{"Serve CPU DRAM for the NDS verbs benchmark."};
    app.add_option("--device", config.transport.backend.device_name)->required();
    app.add_option("--gid-index", config.transport.backend.gid_index)->required();
    app.add_option("--listen", config.transport.listen_address)->required();
    app.add_option("--ib-port", config.transport.backend.port);
    app.add_option("--operation", operation)->required()->check(CLI::IsMember({"read", "write"}));
    app.add_option("--bytes", config.bytes)->required()->check(CLI::Range(1U, UINT32_MAX));
    app.add_option("--in-flight", config.in_flight)->check(CLI::Range(1U, kMaximumInFlight));
    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError &error) {
        return nds::unexpected(nds::ErrorCode::kInvalidArgument,
                               app.exit(error) == 0 ? "help requested" : "invalid options");
    }
    config.operation =
        operation == "read" ? nds::benchmark::VerbsOperation::Read : nds::benchmark::VerbsOperation::Write;
    return config;
}

nds::Result<std::size_t> memory_bytes(const Config &config) {
    if (config.bytes > std::numeric_limits<std::size_t>::max() / config.in_flight)
        return nds::unexpected(nds::ErrorCode::kInvalidArgument, "benchmark memory size overflows address space");
    const std::size_t total = static_cast<std::size_t>(config.bytes) * config.in_flight;
    if (total > UINT32_MAX)
        return nds::unexpected(nds::ErrorCode::kInvalidArgument, "benchmark memory exceeds wire-record capacity");
    return total;
}

}  // namespace

int main(int argc, char **argv) {
    (void)nds::log::configure("verbs-benchmark-server", "stderr", "info");
    const auto config = parse(argc, argv);
    if (!config) {
        NDS_LOG_ERROR("verbs-benchmark-server", "options failed: {}", config.error().message);
        return EXIT_FAILURE;
    }
    const auto bytes = memory_bytes(*config);
    if (!bytes) {
        NDS_LOG_ERROR("verbs-benchmark-server", "invalid memory size: {}", bytes.error().message);
        return EXIT_FAILURE;
    }
    nds::server::TransportListener listener;
    if (const auto opened = listener.open(config->transport); !opened) {
        NDS_LOG_ERROR("verbs-benchmark-server", "listener open failed: {}", opened.error().message);
        return EXIT_FAILURE;
    }
    nds::server::Transport transport;
    if (const auto accepted = listener.accept(&transport); !accepted) {
        NDS_LOG_ERROR("verbs-benchmark-server", "transport setup failed");
        return EXIT_FAILURE;
    }
    std::vector<std::byte> memory(*bytes, std::byte{0x5a});
    const auto access = config->operation == nds::benchmark::VerbsOperation::Read
                            ? nds::server::MemoryAccess::RemoteRead
                            : nds::server::MemoryAccess::RemoteWrite;
    const auto region = transport.register_memory(memory.data(), memory.size(), access);
    if (!region) {
        NDS_LOG_ERROR("verbs-benchmark-server", "CPU memory registration failed: {}", region.error().message);
        return EXIT_FAILURE;
    }
    nds::wire::RemoteMemory peer{};
    const nds::transport::RemoteMemory local{reinterpret_cast<std::uint64_t>(region->address()),
                                             static_cast<std::uint32_t>(region->length()), region->remote_key()};
    if (nds::transport::encode(&local, &peer) != nds::transport::CodecResult::Ok ||
        !transport.exchange_channel()->send(std::as_bytes(std::span{&peer, 1U}))) {
        NDS_LOG_ERROR("verbs-benchmark-server", "remote-memory bootstrap failed");
        return EXIT_FAILURE;
    }
    std::uint8_t finished{};
    if (const auto received = transport.exchange_channel()->receive(std::as_writable_bytes(std::span{&finished, 1U}));
        !received || finished != 1U) {
        NDS_LOG_ERROR("verbs-benchmark-server", "benchmark completion handshake failed");
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

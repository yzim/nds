#include "rdma_benchmark_wire.hh"

#include "nds/logging.hh"
#include "runtime.hh"
#include "transport.hh"

#include <CLI/CLI.hpp>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>

namespace {

struct Config {
    nds::client::RuntimeConfig runtime;
    nds::client::TransportConfig transport;
    nds::benchmark::Operation operation{nds::benchmark::Operation::Read};
    std::uint32_t bytes{};
    std::uint32_t in_flight{64U};
};

nds::Result<Config> parse(int argc, char **argv) {
    Config config;
    std::string operation{"read"};
    CLI::App app{"Publish an NPU HBM region for the CPU-initiated RDMA benchmark."};
    app.add_option("--ascendcl", config.runtime.ascendcl_library)->required();
    app.add_option("--runtime", config.runtime.runtime_library)->required();
    app.add_option("--ra", config.transport.endpoint.ra_library)->required();
    app.add_option("--logical-device", config.runtime.logical_device_id)->required();
    app.add_option("--server", config.transport.server_address)->required();
    app.add_option("--operation", operation)->required()->check(CLI::IsMember({"read", "write"}));
    app.add_option("--bytes", config.bytes)->required()->check(CLI::Range(1U, UINT32_MAX));
    app.add_option("--in-flight", config.in_flight)->check(CLI::Range(1U, UINT32_MAX));
    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError &error) {
        return nds::unexpected(nds::ErrorCode::kInvalidArgument,
                               app.exit(error) == 0 ? "help requested" : "invalid options");
    }
    config.operation = operation == "read" ? nds::benchmark::Operation::Read : nds::benchmark::Operation::Write;
    return config;
}

nds::Result<std::size_t> total_bytes(const Config &config) {
    if (config.in_flight == 0U || config.bytes > std::numeric_limits<std::size_t>::max() / config.in_flight)
        return nds::unexpected(nds::ErrorCode::kInvalidArgument, "benchmark buffer size overflows address space");
    return static_cast<std::size_t>(config.bytes) * config.in_flight;
}

}  // namespace

int main(int argc, char **argv) {
    (void)nds::log::configure("cpu-hbm-peer", "stderr", "info");
    const auto config = parse(argc, argv);
    if (!config) {
        NDS_LOG_ERROR("cpu-hbm-peer", "options failed: {}", config.error().message);
        return EXIT_FAILURE;
    }
    const auto memory_bytes = total_bytes(*config);
    if (!memory_bytes) {
        NDS_LOG_ERROR("cpu-hbm-peer", "invalid buffer size: {}", memory_bytes.error().message);
        return EXIT_FAILURE;
    }
    nds::client::Runtime runtime;
    nds::client::Transport transport;
    if (const auto opened = runtime.open(config->runtime); !opened) {
        NDS_LOG_ERROR("cpu-hbm-peer", "runtime open failed: {}", opened.error().message);
        return EXIT_FAILURE;
    }
    if (const auto opened = transport.open(&runtime, config->transport, {}); !opened) {
        NDS_LOG_ERROR("cpu-hbm-peer", "transport open failed: {}", opened.error().message);
        return EXIT_FAILURE;
    }
    auto buffer = runtime.allocate(*memory_bytes);
    if (!buffer) {
        NDS_LOG_ERROR("cpu-hbm-peer", "NPU HBM allocation failed: {}", buffer.error().message);
        return EXIT_FAILURE;
    }
    auto region = transport.endpoint()->reg_mr(*buffer, nds::client::MemoryAccess::DirectNpu);
    if (!region) {
        NDS_LOG_ERROR("cpu-hbm-peer", "NPU HBM registration failed: {}", region.error().message);
        return EXIT_FAILURE;
    }
    std::array<std::uint8_t, nds::benchmark::kMemoryRecordBytes> record{};
    if (!nds::benchmark::serialize_remote_memory(
            {config->operation, region->address(), region->length(), region->remote_key()}, &record)) {
        NDS_LOG_ERROR("cpu-hbm-peer", "NPU HBM metadata serialization failed");
        return EXIT_FAILURE;
    }
    if (const auto sent = transport.bootstrap()->send_bytes(record.data(), record.size()); !sent) {
        NDS_LOG_ERROR("cpu-hbm-peer", "NPU HBM metadata send failed: {}", sent.error().message);
        return EXIT_FAILURE;
    }
    std::uint8_t finished{};
    if (const auto received = transport.bootstrap()->receive_bytes(&finished, sizeof(finished)); !received || finished != 1U) {
        NDS_LOG_ERROR("cpu-hbm-peer", "CPU benchmark completion handshake failed");
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

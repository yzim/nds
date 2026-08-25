#include "rdma_benchmark_wire.hh"

#include "nds/logging.hh"
#include "transport.hh"

#include <CLI/CLI.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

namespace {

constexpr std::uint32_t kDefaultInFlight = 64U;
constexpr std::uint32_t kMaxInFlight = 1024U;

struct Config {
    nds::server::ConnectionConfig connection;
    nds::benchmark::Operation operation{nds::benchmark::Operation::Read};
    std::uint32_t bytes{};
    std::uint64_t mr_bytes{};
    std::uint32_t in_flight{kDefaultInFlight};
};

nds::Result<Config> parse(int argc, char **argv) {
    Config config;
    std::string operation{"read"};
    CLI::App app{"Serve one CPU DRAM region for the NDS RA verbs benchmark."};
    app.add_option("--device", config.connection.backend.device_name)->required();
    app.add_option("--gid-index", config.connection.backend.gid_index)->required();
    app.add_option("--listen", config.connection.listen_address)->required();
    app.add_option("--ib-port", config.connection.backend.port);
    app.add_option("--operation", operation)->required()->check(CLI::IsMember({"read", "write"}));
    app.add_option("--bytes", config.bytes)->required()->check(CLI::Range(1U, UINT32_MAX));
    app.add_option("--mr-bytes", config.mr_bytes, "Registered MR size; zero selects the window minimum.");
    app.add_option("--in-flight", config.in_flight)->check(CLI::Range(1U, kMaxInFlight));
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
    const std::size_t minimum = static_cast<std::size_t>(config.bytes) * config.in_flight;
    if (config.mr_bytes == 0U)
        return minimum;
    if (config.mr_bytes < minimum || config.mr_bytes > std::numeric_limits<std::size_t>::max())
        return nds::unexpected(nds::ErrorCode::kInvalidArgument, "benchmark MR size is invalid");
    return static_cast<std::size_t>(config.mr_bytes);
}

}  // namespace

int main(int argc, char **argv) {
    (void)nds::log::configure("cpu-server", "stderr", "info");
    const auto config = parse(argc, argv);
    if (!config) {
        NDS_LOG_ERROR("cpu-server", "options failed: {}", config.error().message);
        return EXIT_FAILURE;
    }
    const auto memory_bytes = total_bytes(*config);
    if (!memory_bytes) {
        NDS_LOG_ERROR("cpu-server", "invalid benchmark buffer size: {}", memory_bytes.error().message);
        return EXIT_FAILURE;
    }
    nds::server::Connection connection;
    NDS_LOG_INFO("cpu-server", "opening CPU verbs connection");
    if (const auto opened = connection.open(config->connection); !opened) {
        NDS_LOG_ERROR("cpu-server", "connection open failed: {}", opened.error().message);
        return EXIT_FAILURE;
    }
    std::array<std::byte, 1U> activation_buffer{};
    const auto activation = connection.prepare_receive(activation_buffer.data(), activation_buffer.size());
    if (!activation || !connection.activate()) {
        NDS_LOG_ERROR("cpu-server", "verbs activation failed");
        return EXIT_FAILURE;
    }
    NDS_LOG_INFO("cpu-server", "activated CPU verbs QP");
    std::vector<std::byte> memory(*memory_bytes, std::byte{0x5a});
    const auto access = config->operation == nds::benchmark::Operation::Read ? nds::server::MemoryAccess::RemoteRead
                                                                               : nds::server::MemoryAccess::RemoteWrite;
    auto region = connection.register_memory(memory.data(), memory.size(), access);
    if (!region) {
        NDS_LOG_ERROR("cpu-server", "DRAM registration failed: {}", region.error().message);
        return EXIT_FAILURE;
    }
    NDS_LOG_INFO("cpu-server", "registered {} bytes of CPU DRAM", *memory_bytes);
    std::array<std::uint8_t, nds::benchmark::kMemoryRecordBytes> record{};
    if (!nds::benchmark::serialize_remote_memory(
            {config->operation, reinterpret_cast<std::uint64_t>(region->address()), region->length(), region->remote_key()},
            &record) ||
        !connection.bootstrap()->send_bytes(record.data(), record.size())) {
        NDS_LOG_ERROR("cpu-server", "remote-memory bootstrap failed");
        return EXIT_FAILURE;
    }
    NDS_LOG_INFO("cpu-server", "sent remote-memory metadata");
    std::uint8_t finished{};
    if (const auto received = connection.bootstrap()->receive_bytes(&finished, sizeof(finished)); !received || finished != 1U) {
        NDS_LOG_ERROR("cpu-server", "benchmark completion handshake failed");
        return EXIT_FAILURE;
    }
    NDS_LOG_INFO("cpu-server", "client completed benchmark");
    return EXIT_SUCCESS;
}

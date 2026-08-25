#include "rdma_benchmark_wire.hh"

#include "nds/logging.hh"
#include "runtime.hh"
#include "transport.hh"

#include <CLI/CLI.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <memory>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <utility>
#include <unistd.h>
#include <vector>

namespace {

constexpr std::uint32_t kMaxQps = 8U;
constexpr std::uint32_t kTransportOpenAttempts = 20U;
constexpr auto kTransportOpenRetryDelay = std::chrono::milliseconds(250);

struct Config {
    nds::client::RuntimeConfig runtime;
    nds::client::TransportConfig transport;
    nds::benchmark::Operation operation{nds::benchmark::Operation::Read};
    std::uint32_t bytes{};
    std::uint64_t mr_bytes{};
    std::uint32_t in_flight{64U};
    std::uint32_t qps{1U};
    bool host_pinned{};
};

nds::Result<Config> parse(int argc, char **argv) {
    Config config;
    std::string operation{"read"};
    CLI::App app{"Publish an NPU HBM or NPU-RNIC-registered host-pinned region for the CPU-initiated RDMA benchmark."};
    app.add_option("--ascendcl", config.runtime.ascendcl_library)->required();
    app.add_option("--runtime", config.runtime.runtime_library)->required();
    app.add_option("--ra", config.transport.endpoint.ra_library)->required();
    app.add_option("--logical-device", config.runtime.logical_device_id)->required();
    app.add_option("--server", config.transport.server_address)->required();
    app.add_option("--operation", operation)->required()->check(CLI::IsMember({"read", "write"}));
    app.add_option("--bytes", config.bytes)->required()->check(CLI::Range(1U, UINT32_MAX));
    app.add_option("--mr-bytes", config.mr_bytes, "Registered remote MR size; zero selects the window minimum.");
    app.add_option("--in-flight", config.in_flight)->check(CLI::Range(1U, UINT32_MAX));
    app.add_option("--qps", config.qps)->check(CLI::Range(1U, kMaxQps));
    app.add_flag("--host-pinned", config.host_pinned,
                 "Register AscendCL page-locked host memory through the NPU RNIC instead of NPU HBM.");
    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError &error) {
        return nds::unexpected(nds::ErrorCode::kInvalidArgument,
                               app.exit(error) == 0 ? "help requested" : "invalid options");
    }
    config.operation = operation == "read" ? nds::benchmark::Operation::Read : nds::benchmark::Operation::Write;
    config.transport.tcp_timeout_ms = 10000U;
    return config;
}

nds::Result<std::string> indexed_address(const std::string &base, std::uint32_t index) {
    const auto parsed = nds::parse_tcp_address(base);
    if (!parsed || parsed->port > std::numeric_limits<std::uint16_t>::max() - index)
        return nds::unexpected(nds::ErrorCode::kInvalidArgument, "benchmark QP port range is invalid");
    return parsed->ipv4 + ":" + std::to_string(static_cast<std::uint32_t>(parsed->port) + index);
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

bool is_retryable_transport_open_failure(const nds::Error &error) {
    return error.code == nds::ErrorCode::kTransport &&
           (error.message.starts_with("connect:") ||
            error.message == "TCP bootstrap socket became unavailable");
}

int run_single(const Config &config, std::uint32_t index) {
    const auto memory_bytes = total_bytes(config);
    if (!memory_bytes) {
        NDS_LOG_ERROR("cpu-hbm-peer", "QP {} invalid buffer size: {}", index, memory_bytes.error().message);
        return EXIT_FAILURE;
    }
    nds::client::Runtime runtime;
    if (const auto opened = runtime.open(config.runtime); !opened) {
        NDS_LOG_ERROR("cpu-hbm-peer", "QP {} runtime open failed: {}", index, opened.error().message);
        return EXIT_FAILURE;
    }
    std::unique_ptr<nds::client::Transport> transport;
    for (std::uint32_t attempt = 0U; attempt < kTransportOpenAttempts; ++attempt) {
        auto candidate = std::make_unique<nds::client::Transport>();
        if (const auto opened = candidate->open(&runtime, config.transport, {}); opened) {
            transport = std::move(candidate);
            break;
        } else if (!is_retryable_transport_open_failure(opened.error()) ||
                   attempt + 1U == kTransportOpenAttempts) {
            NDS_LOG_ERROR("cpu-hbm-peer", "QP {} transport open failed: {}", index, opened.error().message);
            return EXIT_FAILURE;
        } else {
            NDS_LOG_INFO("cpu-hbm-peer", "QP {} transport port is not ready; retry {}/{}", index, attempt + 2U,
                         kTransportOpenAttempts);
            std::this_thread::sleep_for(kTransportOpenRetryDelay);
        }
    }
    const auto location = config.host_pinned ? nds::client::MemoryLocation::HostPinned
                                             : nds::client::MemoryLocation::Device;
    auto buffer = runtime.allocate(*memory_bytes, location);
    if (!buffer) {
        NDS_LOG_ERROR("cpu-hbm-peer", "QP {} remote memory allocation failed: {}", index, buffer.error().message);
        return EXIT_FAILURE;
    }
    auto region = transport->endpoint()->reg_mr(*buffer, nds::client::MemoryAccess::DirectNpu);
    if (!region) {
        NDS_LOG_ERROR("cpu-hbm-peer", "QP {} remote memory registration failed: {}", index, region.error().message);
        return EXIT_FAILURE;
    }
    std::array<std::uint8_t, nds::benchmark::kMemoryRecordBytes> record{};
    if (!nds::benchmark::serialize_remote_memory(
            {config.operation, region->address(), region->length(), region->remote_key()}, &record)) {
        NDS_LOG_ERROR("cpu-hbm-peer", "QP {} remote memory metadata serialization failed", index);
        return EXIT_FAILURE;
    }
    if (const auto sent = transport->bootstrap()->send_bytes(record.data(), record.size()); !sent) {
        NDS_LOG_ERROR("cpu-hbm-peer", "QP {} NPU HBM metadata send failed: {}", index, sent.error().message);
        return EXIT_FAILURE;
    }
    std::uint8_t finished{};
    if (const auto received = transport->bootstrap()->receive_bytes(&finished, sizeof(finished));
        !received || finished != 1U) {
        NDS_LOG_ERROR("cpu-hbm-peer", "QP {} CPU benchmark completion handshake failed", index);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

}  // namespace

int main(int argc, char **argv) {
    (void)nds::log::configure("cpu-hbm-peer", "stderr", "info");
    const auto config = parse(argc, argv);
    if (!config) {
        NDS_LOG_ERROR("cpu-hbm-peer", "options failed: {}", config.error().message);
        return EXIT_FAILURE;
    }
    if (config->qps == 1U)
        return run_single(*config, 0U);

    std::vector<pid_t> children;
    children.reserve(config->qps);
    for (std::uint32_t index = 0U; index < config->qps; ++index) {
        const auto server_address = indexed_address(config->transport.server_address, index);
        if (!server_address) {
            NDS_LOG_ERROR("cpu-hbm-peer", "invalid QP {} server address: {}", index,
                          server_address.error().message);
            return EXIT_FAILURE;
        }
        const pid_t child = fork();
        if (child < 0) {
            NDS_LOG_ERROR("cpu-hbm-peer", "fork failed for QP {}", index);
            return EXIT_FAILURE;
        }
        if (child == 0) {
            Config child_config = *config;
            child_config.qps = 1U;
            child_config.transport.server_address = *server_address;
            _exit(run_single(child_config, index));
        }
        children.push_back(child);
    }
    bool failed = false;
    for (const pid_t child : children) {
        int status = 0;
        if (waitpid(child, &status, 0) < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != EXIT_SUCCESS)
            failed = true;
    }
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}

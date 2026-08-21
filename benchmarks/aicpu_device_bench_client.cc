#include "rdma_benchmark_wire.hh"

#include "aicpu/host/launcher.hh"
#include "nds/device_benchmark.h"
#include "nds/logging.hh"
#include "runtime.hh"
#include "transport.hh"

#include <CLI/CLI.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>

namespace {

constexpr std::int32_t kCompletionTimeoutMs = 30000;
constexpr std::uint32_t kDefaultInFlight = 64U;
constexpr std::uint32_t kMaxInFlight = 1024U;
constexpr std::uint32_t kDefaultMaxWrBytes = 64U * 1024U;
constexpr std::uint32_t kDefaultMaxWrsPerWindow = 64U;

struct Config {
    nds::client::RuntimeConfig runtime;
    nds::client::TransportConfig transport;
    nds::client::ExecutionConfig execution;
    nds::benchmark::Operation operation{nds::benchmark::Operation::Read};
    std::uint32_t bytes{};
    std::uint32_t in_flight{kDefaultInFlight};
    std::uint32_t warmup{2U};
    std::uint32_t iterations{100U};
    std::uint32_t max_wr_bytes{kDefaultMaxWrBytes};
    std::uint32_t max_wrs_per_window{kDefaultMaxWrsPerWindow};
};

nds::Result<Config> parse(int argc, char **argv) {
    Config config;
    std::string operation{"read"};
    CLI::App app{"Benchmark RDMA inside one AICPU operator launch."};
    app.add_option("--ascendcl", config.runtime.ascendcl_library)->required();
    app.add_option("--runtime", config.runtime.runtime_library)->required();
    app.add_option("--ra", config.transport.endpoint.ra_library)->required();
    app.add_option("--aicpu-kernel-config", config.execution.aicpu_kernel_config)->required();
    app.add_option("--logical-device", config.runtime.logical_device_id)->required();
    app.add_option("--server", config.transport.server_address)->required();
    app.add_option("--operation", operation)->required()->check(CLI::IsMember({"read", "write"}));
    app.add_option("--bytes", config.bytes)->required()->check(CLI::Range(1U, UINT32_MAX));
    app.add_option("--in-flight", config.in_flight)->check(CLI::Range(1U, kMaxInFlight));
    app.add_option("--warmup", config.warmup)->check(CLI::Range(0U, UINT32_MAX));
    app.add_option("--iterations", config.iterations)->check(CLI::Range(1U, UINT32_MAX));
    app.add_option("--max-wr-bytes", config.max_wr_bytes)->check(CLI::Range(0U, UINT32_MAX));
    app.add_option("--max-wrs-per-window", config.max_wrs_per_window)->check(CLI::Range(1U, UINT32_MAX));
    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError &error) {
        return nds::unexpected(nds::ErrorCode::kInvalidArgument,
                               app.exit(error) == 0 ? "help requested" : "invalid options");
    }
    config.operation = operation == "read" ? nds::benchmark::Operation::Read : nds::benchmark::Operation::Write;
    config.execution.mode = nds::client::NpuExecutionMode::Aicpu;
    config.transport.qp.control_flags |= nds::client::QueuePairCallerPollsCq;
    return config;
}

nds::Result<std::size_t> total_bytes(const Config &config) {
    if (config.in_flight == 0U || config.bytes > std::numeric_limits<std::size_t>::max() / config.in_flight)
        return nds::unexpected(nds::ErrorCode::kInvalidArgument, "benchmark buffer size overflows address space");
    return static_cast<std::size_t>(config.bytes) * config.in_flight;
}

nds::Result<void> launch_benchmark(nds::AicpuEntrypointLauncher *launcher, nds::client::Runtime *runtime,
                                   const NdsDeviceTransport &device_transport,
                                   const nds::client::MemoryRegion &local, const nds::benchmark::RemoteMemory &remote,
                                   const Config &config, std::uint32_t iterations, void *device_result,
                                   NdsDeviceRdmaBenchmarkResult *result) {
    if (launcher == nullptr || runtime == nullptr || device_result == nullptr || result == nullptr)
        return nds::unexpected(nds::ErrorCode::kInvalidArgument, "AICPU benchmark launch has invalid state");
    *result = {};
    if (const auto copied = runtime->copy_host_to_device(device_result, result, sizeof(*result)); !copied)
        return nds::unexpected(copied.error());
    NdsDeviceRdmaBenchmarkArgs args{};
    args.transport = device_transport;
    args.local_address = local.address();
    args.remote_address = remote.address;
    args.local_key = local.local_key();
    args.remote_key = remote.remote_key;
    args.bytes = config.bytes;
    args.iterations = iterations;
    args.in_flight = config.in_flight;
    args.max_wr_bytes = config.max_wr_bytes;
    args.max_wrs_per_window = config.max_wrs_per_window;
    args.operation = config.operation == nds::benchmark::Operation::Read ? NDS_DEVICE_BENCHMARK_READ
                                                                           : NDS_DEVICE_BENCHMARK_WRITE;
    args.result_address = reinterpret_cast<std::uint64_t>(device_result);
    args.return_value = std::numeric_limits<std::int32_t>::min();
    if (const auto launched = launcher->launch_rdma_benchmark_and_wait(&args, kCompletionTimeoutMs); !launched)
        return nds::unexpected(launched.error());
    if (const auto copied = runtime->copy_device_to_host(result, device_result, sizeof(*result)); !copied)
        return nds::unexpected(copied.error());
    if (result->status != NDS_DEVICE_BENCHMARK_SUCCESS)
        return nds::unexpected(nds::ErrorCode::kRa, "AICPU device benchmark failed with status=" +
                                                         std::to_string(result->status) +
                                                         ", completion_status=" +
                                                         std::to_string(result->completion_status) +
                                                         ", vendor_error=" +
                                                         std::to_string(result->completion_vendor_error));
    return {};
}

}  // namespace

int main(int argc, char **argv) {
    (void)nds::log::configure("aicpu-benchmark", "stderr", "info");
    const auto config = parse(argc, argv);
    if (!config) {
        NDS_LOG_ERROR("aicpu-benchmark", "options failed: {}", config.error().message);
        return EXIT_FAILURE;
    }
    nds::client::Runtime runtime;
    nds::client::Transport transport;
    if (const auto opened = runtime.open(config->runtime); !opened) {
        NDS_LOG_ERROR("aicpu-benchmark", "runtime open failed: {}", opened.error().message);
        return EXIT_FAILURE;
    }
    if (const auto opened = transport.open(&runtime, config->transport, config->execution); !opened) {
        NDS_LOG_ERROR("aicpu-benchmark", "transport open failed: {}", opened.error().message);
        return EXIT_FAILURE;
    }
    std::array<std::uint8_t, nds::benchmark::kMemoryRecordBytes> record{};
    if (const auto received = transport.bootstrap()->receive_bytes(record.data(), record.size()); !received) {
        NDS_LOG_ERROR("aicpu-benchmark", "remote-memory bootstrap failed: {}", received.error().message);
        return EXIT_FAILURE;
    }
    nds::benchmark::RemoteMemory remote{};
    const auto required_bytes = total_bytes(*config);
    if (!required_bytes || !nds::benchmark::deserialize_remote_memory(record, &remote) ||
        remote.operation != config->operation || remote.length != *required_bytes) {
        NDS_LOG_ERROR("aicpu-benchmark", "server returned incompatible remote-memory metadata");
        return EXIT_FAILURE;
    }
    auto buffer = runtime.allocate(*required_bytes);
    if (!buffer) {
        NDS_LOG_ERROR("aicpu-benchmark", "NPU allocation failed: {}", buffer.error().message);
        return EXIT_FAILURE;
    }
    auto local = transport.endpoint()->reg_mr(*buffer, nds::client::MemoryAccess::DirectNpu);
    if (!local) {
        NDS_LOG_ERROR("aicpu-benchmark", "NPU MR registration failed: {}", local.error().message);
        return EXIT_FAILURE;
    }
    const auto device_transport = transport.qp()->make_device_transport();
    if (!device_transport) {
        NDS_LOG_ERROR("aicpu-benchmark", "device transport failed: {}", device_transport.error().message);
        return EXIT_FAILURE;
    }
    nds::AicpuEntrypointLauncher launcher;
    if (const auto loaded = launcher.load(&runtime.acl_api(), config->execution.aicpu_kernel_config); !loaded) {
        NDS_LOG_ERROR("aicpu-benchmark", "AICPU launcher load failed: {}", loaded.error().message);
        return EXIT_FAILURE;
    }
    auto device_result = runtime.allocate_device_memory(sizeof(NdsDeviceRdmaBenchmarkResult));
    if (!device_result) {
        NDS_LOG_ERROR("aicpu-benchmark", "result allocation failed: {}", device_result.error().message);
        return EXIT_FAILURE;
    }
    NdsDeviceRdmaBenchmarkResult result{};
    for (std::uint32_t index = 0U; index < config->warmup; ++index) {
        if (const auto warmed = launch_benchmark(&launcher, &runtime, *device_transport, *local, remote, *config, 1U,
                                                 *device_result, &result);
            !warmed)
        {
            NDS_LOG_ERROR("aicpu-benchmark", "warmup failed: {}", warmed.error().message);
            return EXIT_FAILURE;
        }
    }
    const auto start = std::chrono::steady_clock::now();
    if (const auto measured = launch_benchmark(&launcher, &runtime, *device_transport, *local, remote, *config,
                                               config->iterations, *device_result, &result);
        !measured)
    {
        NDS_LOG_ERROR("aicpu-benchmark", "measurement failed: {}", measured.error().message);
        return EXIT_FAILURE;
    }
    const auto elapsed = std::chrono::steady_clock::now() - start;
    (void)runtime.free_device_memory(*device_result);
    const std::uint8_t finished = 1U;
    if (const auto sent = transport.bootstrap()->send_bytes(&finished, sizeof(finished)); !sent) {
        NDS_LOG_ERROR("aicpu-benchmark", "completion handshake failed: {}", sent.error().message);
        return EXIT_FAILURE;
    }
    const double seconds = std::chrono::duration<double>(elapsed).count();
    const double gib_per_second = static_cast<double>(result.bytes_transferred) / seconds /
                                  (1024.0 * 1024.0 * 1024.0);
    std::cout << std::fixed << std::setprecision(3)
              << "{\"backend\":\"aicpu-device-loop\",\"operation\":\""
              << nds::benchmark::operation_name(config->operation) << "\",\"bytes\":" << config->bytes
              << ",\"in_flight\":" << config->in_flight << ",\"iterations\":" << config->iterations
              << ",\"max_wr_bytes\":" << config->max_wr_bytes << ",\"wqe_count\":" << result.wqe_count
              << ",\"poll_count\":" << result.poll_count << ",\"elapsed_ns\":"
              << std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count()
              << ",\"gib_per_second\":" << gib_per_second << "}\n";
    return EXIT_SUCCESS;
}

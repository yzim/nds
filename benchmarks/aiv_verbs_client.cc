#include "rdma_benchmark_wire.hh"

#include "aiv/host/launcher.hh"
#include "nds/logging.hh"
#include "runtime.hh"
#include "transport.hh"

#include <CLI/CLI.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

constexpr std::uint32_t kCompletionTimeoutMs = 5000U;
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
    std::uint32_t warmup{100U};
    std::uint32_t iterations{1000U};
    std::uint32_t max_wr_bytes{kDefaultMaxWrBytes};
    std::uint32_t max_wrs_per_window{kDefaultMaxWrsPerWindow};
};

nds::Result<Config> parse(int argc, char **argv) {
    Config config;
    std::string operation{"read"};
    CLI::App app{"Benchmark AIV RDMA Read/Write from NPU memory to CPU DRAM."};
    app.add_option("--ascendcl", config.runtime.ascendcl_library)->required();
    app.add_option("--runtime", config.runtime.runtime_library)->required();
    app.add_option("--ra", config.transport.endpoint.ra_library)->required();
    app.add_option("--aiv-kernel", config.execution.aiv_kernel)->required();
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
    config.execution.mode = nds::client::NpuExecutionMode::Aiv;
    config.transport.qp.control_flags |= nds::client::QueuePairCallerPollsCq;
    return config;
}

nds::Result<std::size_t> total_bytes(const Config &config) {
    if (config.in_flight == 0U || config.bytes > std::numeric_limits<std::size_t>::max() / config.in_flight)
        return nds::unexpected(nds::ErrorCode::kInvalidArgument, "benchmark buffer size overflows address space");
    return static_cast<std::size_t>(config.bytes) * config.in_flight;
}

nds::Result<void> drain_completions(nds::AivEntrypointLauncher *launcher, nds::client::Runtime *runtime,
                                    const NdsDeviceQp &qp, void *device_poll_buffer, void *device_wc,
                                    std::uint32_t expected) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kCompletionTimeoutMs);
    std::uint32_t completed{};
    NdsDevicePollCqArgs poll{};
    poll.qp = qp;
    poll.is_send_cq = 1U;
    poll.return_value = std::numeric_limits<std::int32_t>::min();
    poll.wc_address = reinterpret_cast<std::uint64_t>(device_wc);
    while (completed < expected && std::chrono::steady_clock::now() < deadline) {
        poll.max_completions = expected - completed;
        if (const auto copied = runtime->copy_host_to_device(device_poll_buffer, &poll, sizeof(poll)); !copied)
            return nds::unexpected(copied.error());
        if (const auto launched =
                launcher->launch_poll_cq_and_wait(reinterpret_cast<std::uint64_t>(device_poll_buffer),
                                                  kCompletionTimeoutMs);
            !launched) {
            return nds::unexpected(launched.error());
        }
        if (const auto copied = runtime->copy_device_to_host(&poll, device_poll_buffer, sizeof(poll)); !copied)
            return nds::unexpected(copied.error());
        if (poll.return_value < 0)
            return nds::unexpected(nds::ErrorCode::kRa, "AIV PollCq returned error: " +
                                                             std::to_string(poll.return_value));
        completed += static_cast<std::uint32_t>(poll.return_value);
    }
    if (completed >= expected)
        return {};
    return nds::unexpected(nds::ErrorCode::kRa,
                           "timed out waiting for AIV send completions: " + std::to_string(completed) + "/" +
                               std::to_string(expected));
}

nds::Result<void> transfer_window(nds::AivEntrypointLauncher *launcher, nds::client::Runtime *runtime,
                                  const NdsDeviceTransport &device_transport, const nds::client::MemoryRegion &local,
                                  const nds::benchmark::RemoteMemory &remote, std::uint32_t bytes,
                                  std::uint32_t request_count, std::uint64_t *next_wr_id,
                                  std::uint32_t max_wr_bytes, void *device_args_buffer, void *device_poll_buffer,
                                  void *device_wc) {
    if (next_wr_id == nullptr || request_count == 0U)
        return nds::unexpected(nds::ErrorCode::kInvalidArgument, "benchmark window requires requests and a WR ID");
    const std::uint32_t chunk_bytes = max_wr_bytes == 0U ? bytes : std::min(bytes, max_wr_bytes);
    const std::uint32_t expected_completions = request_count;
    const bool is_read = remote.operation == nds::benchmark::Operation::Read;
    for (std::uint32_t request = 0U; request < request_count; ++request) {
        const std::uint64_t base_offset = static_cast<std::uint64_t>(request) * bytes;
        const NdsDeviceSendWr wr{(*next_wr_id)++,
                                 is_read ? NDS_DEVICE_WR_RDMA_READ : NDS_DEVICE_WR_RDMA_WRITE,
                                 static_cast<std::uint32_t>(NDS_DEVICE_SEND_SIGNALED),
                                 {local.address() + base_offset, chunk_bytes, local.local_key()},
                                 remote.address + base_offset,
                                 remote.remote_key,
                                 0U};
        if (is_read) {
            NdsDeviceRdmaReadArgs args{};
            args.transport = device_transport;
            args.wr = wr;
            args.return_value = std::numeric_limits<std::int32_t>::min();
            if (const auto copied = runtime->copy_host_to_device(device_args_buffer, &args, sizeof(args)); !copied)
                return nds::unexpected(copied.error());
            if (const auto launched =
                    launcher->launch_rdma_read_and_wait(reinterpret_cast<std::uint64_t>(device_args_buffer),
                                                        kCompletionTimeoutMs);
                !launched)
                return nds::unexpected(launched.error());
            if (const auto copied = runtime->copy_device_to_host(&args, device_args_buffer, sizeof(args)); !copied)
                return nds::unexpected(copied.error());
            if (args.return_value != 0)
                return nds::unexpected(nds::ErrorCode::kRa, "AIV RDMA read failed: " +
                                                                 std::to_string(args.return_value));
        } else {
            NdsDeviceRdmaWriteArgs args{};
            args.transport = device_transport;
            args.wr = wr;
            args.return_value = std::numeric_limits<std::int32_t>::min();
            if (const auto copied = runtime->copy_host_to_device(device_args_buffer, &args, sizeof(args)); !copied)
                return nds::unexpected(copied.error());
            if (const auto launched =
                    launcher->launch_rdma_write_and_wait(reinterpret_cast<std::uint64_t>(device_args_buffer),
                                                         kCompletionTimeoutMs);
                !launched)
                return nds::unexpected(launched.error());
            if (const auto copied = runtime->copy_device_to_host(&args, device_args_buffer, sizeof(args)); !copied)
                return nds::unexpected(copied.error());
            if (args.return_value != 0)
                return nds::unexpected(nds::ErrorCode::kRa, "AIV RDMA write failed: " +
                                                                 std::to_string(args.return_value));
        }
    }
    return drain_completions(launcher, runtime, device_transport.control_qp, device_poll_buffer, device_wc,
                             expected_completions);
}

nds::Result<void> transfer_operations(nds::AivEntrypointLauncher *launcher, nds::client::Runtime *runtime,
                                      const NdsDeviceTransport &device_transport,
                                      const nds::client::MemoryRegion &local,
                                      const nds::benchmark::RemoteMemory &remote, std::uint32_t bytes,
                                      std::uint32_t in_flight, std::uint32_t operations, std::uint64_t *next_wr_id,
                                      std::uint32_t max_wr_bytes, std::uint32_t max_wrs_per_window,
                                      void *device_args_buffer, void *device_poll_buffer, void *device_wc) {
    const std::uint32_t chunk_bytes = max_wr_bytes == 0U ? bytes : std::min(bytes, max_wr_bytes);
    const std::uint64_t wrs_per_request = (static_cast<std::uint64_t>(bytes) + chunk_bytes - 1U) / chunk_bytes;
    if (wrs_per_request > max_wrs_per_window)
        return nds::unexpected(nds::ErrorCode::kInvalidArgument,
                               "max WQEs per window is smaller than one logical transfer");
    const std::uint32_t requests_per_window = static_cast<std::uint32_t>(
        std::min<std::uint64_t>(in_flight, max_wrs_per_window / wrs_per_request));
    for (std::uint32_t completed = 0U; completed < operations;) {
        const std::uint32_t window = std::min(requests_per_window, operations - completed);
        if (const auto transferred = transfer_window(launcher, runtime, device_transport, local, remote, bytes, window,
                                                     next_wr_id, max_wr_bytes, device_args_buffer, device_poll_buffer,
                                                     device_wc);
            !transferred) {
            return nds::unexpected(transferred.error());
        }
        completed += window;
    }
    return {};
}

}  // namespace

int main(int argc, char **argv) {
    (void)nds::log::configure("npu-client", "stderr", "info");
    const auto config = parse(argc, argv);
    if (!config) {
        NDS_LOG_ERROR("npu-client", "options failed: {}", config.error().message);
        return EXIT_FAILURE;
    }
    nds::client::Runtime runtime;
    nds::client::Transport transport;
    NDS_LOG_INFO("npu-client", "opening Ascend runtime");
    if (const auto opened = runtime.open(config->runtime); !opened) {
        NDS_LOG_ERROR("npu-client", "runtime open failed: {}", opened.error().message);
        return EXIT_FAILURE;
    }
    NDS_LOG_INFO("npu-client", "opening AIV verbs transport");
    if (const auto opened = transport.open(&runtime, config->transport, config->execution); !opened) {
        NDS_LOG_ERROR("npu-client", "transport open failed: {}", opened.error().message);
        return EXIT_FAILURE;
    }
    NDS_LOG_INFO("npu-client", "AIV verbs transport is connected");
    std::array<std::uint8_t, nds::benchmark::kMemoryRecordBytes> record{};
    if (const auto received = transport.bootstrap()->receive_bytes(record.data(), record.size()); !received) {
        NDS_LOG_ERROR("npu-client", "remote-memory bootstrap failed: {}", received.error().message);
        return EXIT_FAILURE;
    }
    nds::benchmark::RemoteMemory remote{};
    const auto required_bytes = total_bytes(*config);
    if (!required_bytes) {
        NDS_LOG_ERROR("npu-client", "invalid benchmark buffer size: {}", required_bytes.error().message);
        return EXIT_FAILURE;
    }
    if (!nds::benchmark::deserialize_remote_memory(record, &remote) || remote.operation != config->operation ||
        remote.length != *required_bytes) {
        NDS_LOG_ERROR("npu-client", "server returned incompatible remote-memory metadata");
        return EXIT_FAILURE;
    }
    NDS_LOG_INFO("npu-client", "received remote-memory metadata");
    auto buffer = runtime.allocate(*required_bytes);
    if (!buffer) {
        NDS_LOG_ERROR("npu-client", "NPU memory allocation failed: {}", buffer.error().message);
        return EXIT_FAILURE;
    }
    auto local = transport.endpoint()->reg_mr(*buffer, nds::client::MemoryAccess::DirectNpu);
    if (!local) {
        NDS_LOG_ERROR("npu-client", "NPU memory registration failed: {}", local.error().message);
        return EXIT_FAILURE;
    }
    NDS_LOG_INFO("npu-client", "registered {} bytes of NPU memory", *required_bytes);
    const auto device_transport = transport.qp()->make_device_transport();
    if (!device_transport) {
        NDS_LOG_ERROR("npu-client", "device transport creation failed: {}", device_transport.error().message);
        return EXIT_FAILURE;
    }
    nds::AivEntrypointLauncher launcher;
    if (const auto loaded = launcher.load(&runtime.acl_api(), config->execution.aiv_kernel); !loaded) {
        NDS_LOG_ERROR("npu-client", "AIV launcher load failed: {}", loaded.error().message);
        return EXIT_FAILURE;
    }
    const auto args_size = sizeof(NdsDeviceRdmaWriteArgs);  // Read and Write args share the same size
    auto device_args_buffer = runtime.allocate_device_memory(args_size);
    if (!device_args_buffer) {
        NDS_LOG_ERROR("npu-client", "device args buffer allocation failed: {}", device_args_buffer.error().message);
        return EXIT_FAILURE;
    }
    auto device_poll_buffer = runtime.allocate_device_memory(sizeof(NdsDevicePollCqArgs));
    if (!device_poll_buffer) {
        NDS_LOG_ERROR("npu-client", "device poll buffer allocation failed: {}", device_poll_buffer.error().message);
        (void)runtime.free_device_memory(*device_args_buffer);
        return EXIT_FAILURE;
    }
    const auto wc_bytes = sizeof(NdsDeviceWc) * NDS_DEVICE_MAX_COMPLETIONS;
    auto device_wc = runtime.allocate_device_memory(wc_bytes);
    if (!device_wc) {
        NDS_LOG_ERROR("npu-client", "device WC buffer allocation failed: {}", device_wc.error().message);
        (void)runtime.free_device_memory(*device_args_buffer);
        (void)runtime.free_device_memory(*device_poll_buffer);
        return EXIT_FAILURE;
    }
    std::uint64_t wr_id = 1U;
    NDS_LOG_INFO("npu-client", "starting {} warmup operations", config->warmup);
    if (const auto completed = transfer_operations(&launcher, &runtime, *device_transport, *local, remote,
                                                   config->bytes, config->in_flight, config->warmup, &wr_id,
                                                   config->max_wr_bytes, config->max_wrs_per_window,
                                                   *device_args_buffer, *device_poll_buffer, *device_wc);
        !completed) {
        NDS_LOG_ERROR("npu-client", "warmup failed: {}", completed.error().message);
        return EXIT_FAILURE;
    }
    NDS_LOG_INFO("npu-client", "starting {} measured operations", config->iterations);
    const auto start = std::chrono::steady_clock::now();
    if (const auto completed = transfer_operations(&launcher, &runtime, *device_transport, *local, remote,
                                                   config->bytes, config->in_flight, config->iterations, &wr_id,
                                                   config->max_wr_bytes, config->max_wrs_per_window,
                                                   *device_args_buffer, *device_poll_buffer, *device_wc);
        !completed) {
        NDS_LOG_ERROR("npu-client", "measurement failed: {}", completed.error().message);
        return EXIT_FAILURE;
    }
    const auto elapsed = std::chrono::steady_clock::now() - start;
    const double seconds = std::chrono::duration<double>(elapsed).count();
    const double ops_per_second = static_cast<double>(config->iterations) / seconds;
    const double gib_per_second = static_cast<double>(config->iterations) * static_cast<double>(config->bytes) /
                                  seconds / (1024.0 * 1024.0 * 1024.0);
    const std::uint32_t output_chunk_bytes =
        config->max_wr_bytes == 0U ? config->bytes : std::min(config->bytes, config->max_wr_bytes);
    (void)runtime.free_device_memory(*device_args_buffer);
    (void)runtime.free_device_memory(*device_poll_buffer);
    (void)runtime.free_device_memory(*device_wc);
    const std::uint8_t finished = 1U;
    if (const auto sent = transport.bootstrap()->send_bytes(&finished, sizeof(finished)); !sent) {
        NDS_LOG_ERROR("npu-client", "benchmark completion handshake failed: {}", sent.error().message);
        return EXIT_FAILURE;
    }
    std::cout << std::fixed << std::setprecision(3) << "{\"backend\":\"aiv\",\"operation\":\""
              << nds::benchmark::operation_name(config->operation) << "\",\"bytes\":" << config->bytes
              << ",\"in_flight\":" << config->in_flight << ",\"warmup\":" << config->warmup
              << ",\"iterations\":" << config->iterations << ",\"max_wr_bytes\":" << output_chunk_bytes
              << ",\"max_wrs_per_window\":" << config->max_wrs_per_window
              << ",\"completion_policy\":\"per-wqe-signaled\""
              << ",\"elapsed_ns\":"
              << std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count() << ",\"ops_per_second\":"
              << ops_per_second << ",\"gib_per_second\":" << gib_per_second << "}" << std::endl;
    return EXIT_SUCCESS;
}
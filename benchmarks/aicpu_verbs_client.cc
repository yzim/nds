#include "rdma_benchmark_wire.hh"

#include "aicpu/host/launcher.hh"
#include "ra/ra.hh"
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
#include <numeric>
#include <random>
#include <span>
#include <string>
#include <vector>

namespace {

constexpr std::uint32_t kCompletionTimeoutMs = 5000U;
constexpr std::uint32_t kDefaultInFlight = 64U;
constexpr std::uint32_t kMaxInFlight = 1024U;
constexpr std::uint32_t kDefaultMaxWrBytes = 64U * 1024U;
constexpr std::uint32_t kDefaultMaxWrsPerWindow = 64U;
constexpr std::uint64_t kRandomOffsetSeed = UINT64_C(0x4e44534149435055);

struct Config {
    nds::client::RuntimeConfig runtime;
    nds::client::TransportConfig transport;
    nds::client::ExecutionConfig execution;
    nds::benchmark::Operation operation{nds::benchmark::Operation::Read};
    std::uint32_t bytes{};
    std::uint64_t mr_bytes{};
    std::uint32_t in_flight{kDefaultInFlight};
    std::uint32_t warmup{100U};
    std::uint32_t iterations{1000U};
    std::uint32_t max_wr_bytes{kDefaultMaxWrBytes};
    std::uint32_t max_wrs_per_window{kDefaultMaxWrsPerWindow};
    std::string aicpu_qp_mode{"normal"};
    std::uint32_t wr_per_doorbell{1U};
    std::uint32_t wr_per_signal{1U};
    bool random_addresses{};
};

nds::Result<Config> parse(int argc, char **argv) {
    Config config;
    std::string operation{"read"};
    CLI::App app{"Benchmark AICPU RDMA Read/Write from NPU memory to CPU DRAM."};
    app.add_option("--ascendcl", config.runtime.ascendcl_library)->required();
    app.add_option("--runtime", config.runtime.runtime_library)->required();
    app.add_option("--ra", config.transport.endpoint.ra_library)->required();
    app.add_option("--aicpu-kernel-config", config.execution.aicpu_kernel_config)->required();
    app.add_option("--logical-device", config.runtime.logical_device_id)->required();
    app.add_option("--server", config.transport.server_address)->required();
    app.add_option("--operation", operation)->required()->check(CLI::IsMember({"read", "write"}));
    app.add_option("--bytes", config.bytes)->required()->check(CLI::Range(1U, UINT32_MAX));
    app.add_option("--mr-bytes", config.mr_bytes, "Registered MR size; zero selects the workload minimum.");
    app.add_option("--in-flight", config.in_flight)->check(CLI::Range(1U, kMaxInFlight));
    app.add_option("--warmup", config.warmup)->check(CLI::Range(0U, UINT32_MAX));
    app.add_option("--iterations", config.iterations)->check(CLI::Range(1U, UINT32_MAX));
    app.add_option("--max-wr-bytes", config.max_wr_bytes)->check(CLI::Range(0U, UINT32_MAX));
    app.add_option("--max-wrs-per-window", config.max_wrs_per_window)->check(CLI::Range(1U, UINT32_MAX));
    app.add_option("--aicpu-qp-mode", config.aicpu_qp_mode)->check(CLI::IsMember({"normal", "opbase-ext"}));
    app.add_option("--wr-per-doorbell", config.wr_per_doorbell)->check(CLI::Range(1U, UINT32_MAX));
    app.add_option("--wr-per-signal", config.wr_per_signal)->check(CLI::Range(1U, UINT32_MAX));
    app.add_flag("--random-addresses", config.random_addresses,
                 "Use unique reproducible offsets within the local and remote MRs.");
    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError &error) {
        return nds::unexpected(nds::ErrorCode::kInvalidArgument,
                               app.exit(error) == 0 ? "help requested" : "invalid options");
    }
    config.operation = operation == "read" ? nds::benchmark::Operation::Read : nds::benchmark::Operation::Write;
    config.execution.mode = nds::client::NpuExecutionMode::Aicpu;
    config.transport.qp.control_flags |= nds::client::QueuePairCallerPollsCq;
    config.transport.qp.ai_qp_mode = config.aicpu_qp_mode == "opbase-ext" ? NDS_RA_QP_MODE_OPBASE_EXT
                                                                             : NDS_RA_QP_MODE_NORMAL;
    if (config.aicpu_qp_mode == "opbase-ext" &&
        (config.operation != nds::benchmark::Operation::Write || config.wr_per_doorbell != config.wr_per_signal)) {
        return nds::unexpected(nds::ErrorCode::kInvalidArgument,
                               "AICPU OPBASE_EXT batch posting requires Write with matching doorbell and signal groups");
    }
    if (config.random_addresses && config.aicpu_qp_mode == "opbase-ext")
        return nds::unexpected(nds::ErrorCode::kInvalidArgument,
                               "random addresses are unsupported by the OPBASE_EXT batch diagnostic");
    return config;
}

nds::Result<std::size_t> total_bytes(const Config &config) {
    const std::uint64_t request_count = config.random_addresses
                                            ? static_cast<std::uint64_t>(config.warmup) + config.iterations
                                            : config.in_flight;
    if (request_count == 0U || config.bytes > std::numeric_limits<std::size_t>::max() / request_count)
        return nds::unexpected(nds::ErrorCode::kInvalidArgument, "benchmark buffer size overflows address space");
    const std::size_t minimum = static_cast<std::size_t>(config.bytes) * request_count;
    if (config.mr_bytes == 0U)
        return minimum;
    if (config.mr_bytes < minimum || config.mr_bytes > std::numeric_limits<std::size_t>::max())
        return nds::unexpected(nds::ErrorCode::kInvalidArgument, "benchmark MR size is invalid");
    return static_cast<std::size_t>(config.mr_bytes);
}

nds::Result<std::vector<std::uint64_t>> make_offsets(const Config &config, std::size_t memory_bytes) {
    const std::uint64_t count = static_cast<std::uint64_t>(config.warmup) + config.iterations;
    std::vector<std::uint64_t> offsets(static_cast<std::size_t>(count));
    if (!config.random_addresses) {
        for (std::uint64_t index = 0U; index < count; ++index)
            offsets[static_cast<std::size_t>(index)] = (index % config.in_flight) * config.bytes;
        return offsets;
    }
    const std::uint64_t slots = memory_bytes / config.bytes;
    if (slots < count)
        return nds::unexpected(nds::ErrorCode::kInvalidArgument, "MR does not contain enough unique request slots");
    std::vector<std::uint64_t> indices(static_cast<std::size_t>(slots));
    std::iota(indices.begin(), indices.end(), 0U);
    std::mt19937_64 generator(kRandomOffsetSeed);
    std::shuffle(indices.begin(), indices.end(), generator);
    for (std::uint64_t index = 0U; index < count; ++index)
        offsets[static_cast<std::size_t>(index)] = indices[static_cast<std::size_t>(index)] * config.bytes;
    return offsets;
}

nds::Result<void> drain_completions(nds::AicpuEntrypointLauncher *launcher, nds::client::Runtime *runtime,
                                    const NdsDeviceQp &qp, void *device_wc, std::uint32_t expected) {
    if (launcher == nullptr || runtime == nullptr || device_wc == nullptr)
        return nds::unexpected(nds::ErrorCode::kInvalidArgument, "AICPU completion polling requires runtime state");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kCompletionTimeoutMs);
    std::uint32_t completed{};
    while (completed < expected && std::chrono::steady_clock::now() < deadline) {
        NdsDeviceWc marker{};
        marker.status = std::numeric_limits<std::int32_t>::min();
        if (const auto copied = runtime->copy_host_to_device(device_wc, &marker, sizeof(marker)); !copied)
            return nds::unexpected(copied.error());
        NdsDevicePollCqArgs poll{};
        poll.qp = qp;
        poll.is_send_cq = 1U;
        poll.max_completions = 1U;
        poll.wc_address = reinterpret_cast<std::uint64_t>(device_wc);
        poll.return_value = std::numeric_limits<std::int32_t>::min();
        if (const auto launched = launcher->launch_poll_cq_and_wait(&poll, kCompletionTimeoutMs); !launched) {
            return nds::unexpected(launched.error());
        }
        NdsDeviceWc observed{};
        if (const auto copied = runtime->copy_device_to_host(&observed, device_wc, sizeof(observed)); !copied)
            return nds::unexpected(copied.error());
        if (observed.status == std::numeric_limits<std::int32_t>::min())
            continue;
        if (observed.status != 0)
            return nds::unexpected(nds::ErrorCode::kRa, "AICPU send completion failed: status=" +
                                                             std::to_string(observed.status) +
                                                             ", vendor_error=" +
                                                             std::to_string(observed.vendor_error));
        ++completed;
    }
    if (completed >= expected)
        return {};
    return nds::unexpected(nds::ErrorCode::kRa,
                           "timed out waiting for AICPU send completions: " + std::to_string(completed) + "/" +
                               std::to_string(expected));
}

nds::Result<void> transfer_window(nds::AicpuEntrypointLauncher *launcher, nds::client::Runtime *runtime,
                                  const NdsDeviceTransport &device_transport,
                                  const nds::client::MemoryRegion &local, const nds::benchmark::RemoteMemory &remote,
                                  std::uint32_t bytes, std::span<const std::uint64_t> offsets,
                                  std::uint64_t first_request, std::uint32_t request_count, std::uint64_t *next_wr_id,
                                  std::uint32_t max_wr_bytes, void *device_wc, std::uint32_t wr_per_doorbell,
                                  std::uint32_t wr_per_signal) {
    if (next_wr_id == nullptr || request_count == 0U)
        return nds::unexpected(nds::ErrorCode::kInvalidArgument, "benchmark window requires requests and a WR ID");
    const std::uint32_t chunk_bytes = max_wr_bytes == 0U ? bytes : std::min(bytes, max_wr_bytes);
    const std::uint64_t chunks_per_request =
        (static_cast<std::uint64_t>(bytes) + chunk_bytes - 1U) / chunk_bytes;
    const std::uint64_t total_wrs = static_cast<std::uint64_t>(request_count) * chunks_per_request;
    if (total_wrs > std::numeric_limits<std::uint32_t>::max())
        return nds::unexpected(nds::ErrorCode::kInvalidArgument, "benchmark WQE count exceeds supported range");
    if (device_transport.control_qp.qp_mode == NDS_DEVICE_QP_MODE_OPBASE_EXT) {
        if (remote.operation != nds::benchmark::Operation::Write || chunks_per_request != 1U ||
            wr_per_doorbell != wr_per_signal)
            return nds::unexpected(nds::ErrorCode::kInvalidArgument,
                                   "AICPU OPBASE_EXT batch path requires one-WQE Writes and matching groups");
        std::uint32_t expected_completions{};
        std::uint32_t submitted{};
        while (submitted < request_count) {
            const std::uint32_t group = std::min(wr_per_doorbell, request_count - submitted);
            NdsDevicePostSendBatchArgs batch{};
            batch.qp = device_transport.control_qp;
            batch.local_address = local.address() + static_cast<std::uint64_t>(submitted) * bytes;
            batch.remote_address = remote.address + static_cast<std::uint64_t>(submitted) * bytes;
            batch.wr_id_start = *next_wr_id;
            batch.local_key = local.local_key();
            batch.remote_key = remote.remote_key;
            batch.length = bytes;
            batch.count = group;
            batch.doorbell_address = reinterpret_cast<std::uint64_t>(device_wc);
            if (const auto launched = launcher->launch_post_send_batch_and_wait(&batch, kCompletionTimeoutMs);
                !launched)
                return nds::unexpected(launched.error());
            NdsDeviceDoorbell doorbell{};
            if (const auto copied = runtime->copy_device_to_host(&doorbell, device_wc, sizeof(doorbell)); !copied)
                return nds::unexpected(copied.error());
            if (doorbell.reserved != 0U)
                return nds::unexpected(nds::ErrorCode::kRa,
                                       "AICPU batch provider post failed before doorbell: " +
                                           std::to_string(static_cast<std::int32_t>(doorbell.reserved)));
            NdsRaSendResponse response{};
            response.doorbell.db_index = doorbell.index;
            response.doorbell.db_info = doorbell.info;
            if (const auto rung = nds::NdsRaRingSend(runtime, response); !rung)
                return nds::unexpected(rung.error());
            *next_wr_id += group;
            ++expected_completions;
            submitted += group;
        }
        return drain_completions(launcher, runtime, device_transport.control_qp, device_wc, expected_completions);
    }
    const std::uint32_t expected_completions = static_cast<std::uint32_t>(total_wrs);
    const bool is_read = remote.operation == nds::benchmark::Operation::Read;
    for (std::uint32_t request = 0U; request < request_count; ++request) {
        const std::uint64_t base_offset = offsets[static_cast<std::size_t>(first_request + request)];
        for (std::uint64_t chunk_offset = 0U; chunk_offset < bytes; chunk_offset += chunk_bytes) {
            const std::uint32_t length =
                std::min(chunk_bytes, static_cast<std::uint32_t>(bytes - chunk_offset));
            const NdsDeviceSendWr wr{(*next_wr_id)++,
                                     is_read ? NDS_DEVICE_WR_RDMA_READ : NDS_DEVICE_WR_RDMA_WRITE,
                                     static_cast<std::uint32_t>(NDS_DEVICE_SEND_SIGNALED),
                                     {local.address() + base_offset + chunk_offset, length, local.local_key()},
                                     remote.address + base_offset + chunk_offset,
                                     remote.remote_key,
                                     0U};
            if (is_read) {
                NdsDeviceRdmaReadArgs args{};
                args.transport = device_transport;
                args.wr = wr;
                args.return_value = std::numeric_limits<std::int32_t>::min();
                if (const auto launched = launcher->launch_rdma_read_and_wait(&args, kCompletionTimeoutMs);
                    !launched)
                    return nds::unexpected(launched.error());
            } else {
                NdsDeviceRdmaWriteArgs args{};
                args.transport = device_transport;
                args.wr = wr;
                args.return_value = std::numeric_limits<std::int32_t>::min();
                if (const auto launched = launcher->launch_rdma_write_and_wait(&args, kCompletionTimeoutMs);
                    !launched)
                    return nds::unexpected(launched.error());
            }
        }
    }
    return drain_completions(launcher, runtime, device_transport.control_qp, device_wc, expected_completions);
}

nds::Result<void> transfer_operations(nds::AicpuEntrypointLauncher *launcher, nds::client::Runtime *runtime,
                                      const NdsDeviceTransport &device_transport,
                                      const nds::client::MemoryRegion &local,
                                      const nds::benchmark::RemoteMemory &remote, std::uint32_t bytes,
                                      std::uint32_t in_flight, std::uint32_t operations, std::uint64_t *next_wr_id,
                                      std::uint32_t max_wr_bytes, std::uint32_t max_wrs_per_window,
                                      void *device_wc, std::uint32_t wr_per_doorbell, std::uint32_t wr_per_signal,
                                      std::span<const std::uint64_t> offsets, std::uint64_t first_request) {
    const std::uint32_t chunk_bytes = max_wr_bytes == 0U ? bytes : std::min(bytes, max_wr_bytes);
    const std::uint64_t wrs_per_request = (static_cast<std::uint64_t>(bytes) + chunk_bytes - 1U) / chunk_bytes;
    if (wrs_per_request > max_wrs_per_window)
        return nds::unexpected(nds::ErrorCode::kInvalidArgument,
                               "max WQEs per window is smaller than one logical transfer");
    const std::uint32_t requests_per_window = static_cast<std::uint32_t>(
        std::min<std::uint64_t>(in_flight, max_wrs_per_window / wrs_per_request));
    for (std::uint32_t completed = 0U; completed < operations;) {
        const std::uint32_t window = std::min(requests_per_window, operations - completed);
        if (const auto transferred = transfer_window(launcher, runtime, device_transport, local, remote, bytes, offsets,
                                                     first_request + completed, window, next_wr_id, max_wr_bytes,
                                                     device_wc, wr_per_doorbell, wr_per_signal);
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
    NDS_LOG_INFO("npu-client", "opening AICPU verbs transport");
    if (const auto opened = transport.open(&runtime, config->transport, config->execution); !opened) {
        NDS_LOG_ERROR("npu-client", "transport open failed: {}", opened.error().message);
        return EXIT_FAILURE;
    }
    NDS_LOG_INFO("npu-client", "AICPU verbs transport is connected");
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
        remote.length < *required_bytes) {
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
    const auto offsets = make_offsets(*config, *required_bytes);
    if (!offsets) {
        NDS_LOG_ERROR("npu-client", "address generation failed: {}", offsets.error().message);
        return EXIT_FAILURE;
    }
    const auto device_transport = transport.qp()->make_device_transport();
    if (!device_transport) {
        NDS_LOG_ERROR("npu-client", "device transport creation failed: {}", device_transport.error().message);
        return EXIT_FAILURE;
    }
    nds::AicpuEntrypointLauncher launcher;
    if (const auto loaded = launcher.load(&runtime.acl_api(), config->execution.aicpu_kernel_config);
        !loaded) {
        NDS_LOG_ERROR("npu-client", "AICPU launcher load failed: {}", loaded.error().message);
        return EXIT_FAILURE;
    }
    const auto wc_bytes = sizeof(NdsDeviceWc) * NDS_DEVICE_MAX_COMPLETIONS;
    auto device_wc = runtime.allocate_device_memory(wc_bytes);
    if (!device_wc) {
        NDS_LOG_ERROR("npu-client", "device WC buffer allocation failed: {}", device_wc.error().message);
        return EXIT_FAILURE;
    }
    std::uint64_t wr_id = 1U;
    NDS_LOG_INFO("npu-client", "starting {} warmup operations", config->warmup);
    if (const auto completed = transfer_operations(&launcher, &runtime, *device_transport, *local, remote, config->bytes,
                                                   config->in_flight, config->warmup, &wr_id,
                                                   config->max_wr_bytes, config->max_wrs_per_window, *device_wc,
                                                   config->wr_per_doorbell, config->wr_per_signal, *offsets, 0U);
        !completed) {
        NDS_LOG_ERROR("npu-client", "warmup failed: {}", completed.error().message);
        return EXIT_FAILURE;
    }
    NDS_LOG_INFO("npu-client", "starting {} measured operations", config->iterations);
    const auto start = std::chrono::steady_clock::now();
    if (const auto completed = transfer_operations(&launcher, &runtime, *device_transport, *local, remote, config->bytes,
                                                   config->in_flight, config->iterations, &wr_id,
                                                   config->max_wr_bytes, config->max_wrs_per_window, *device_wc,
                                                   config->wr_per_doorbell, config->wr_per_signal, *offsets,
                                                   config->warmup);
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
    (void)runtime.free_device_memory(*device_wc);
    const std::uint8_t finished = 1U;
    if (const auto sent = transport.bootstrap()->send_bytes(&finished, sizeof(finished)); !sent) {
        NDS_LOG_ERROR("npu-client", "benchmark completion handshake failed: {}", sent.error().message);
        return EXIT_FAILURE;
    }
    std::cout << std::fixed << std::setprecision(3) << "{\"backend\":\"aicpu\",\"operation\":\""
              << nds::benchmark::operation_name(config->operation) << "\",\"bytes\":" << config->bytes
              << ",\"mr_bytes\":" << *required_bytes
              << ",\"in_flight\":" << config->in_flight << ",\"warmup\":" << config->warmup
              << ",\"iterations\":" << config->iterations << ",\"max_wr_bytes\":" << output_chunk_bytes
              << ",\"max_wrs_per_window\":" << config->max_wrs_per_window
              << ",\"aicpu_qp_mode\":\"" << config->aicpu_qp_mode << "\""
              << ",\"wr_per_doorbell\":" << config->wr_per_doorbell
              << ",\"wr_per_signal\":" << config->wr_per_signal
              << ",\"address_policy\":\""
              << (config->random_addresses ? "random-unique" : "cyclic-contiguous") << "\""
              << ",\"completion_policy\":\""
              << (config->aicpu_qp_mode == "opbase-ext" ? "aicpu-group-signaled" : "per-wqe-signaled") << "\""
              << ",\"elapsed_ns\":"
              << std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count() << ",\"ops_per_second\":"
              << ops_per_second << ",\"gib_per_second\":" << gib_per_second << "}" << std::endl;
    return EXIT_SUCCESS;
}

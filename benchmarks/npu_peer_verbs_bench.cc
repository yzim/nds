#include "rdma_benchmark_wire.hh"

#include "aicpu/host/launcher.hh"
#include "aiv/host/launcher.hh"
#include "nds/device_benchmark.h"
#include "nds/logging.hh"
#include "ra/ra.hh"
#include "runtime.hh"
#include "transport.hh"

#include <CLI/CLI.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <barrier>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <numeric>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr std::uint32_t kDefaultInFlight = 64U;
constexpr std::uint32_t kMaxInFlight = 1024U;
constexpr std::uint32_t kMaxQps = 16U;
constexpr std::uint32_t kDefaultSendQueueDepth = 2048U;
constexpr std::uint32_t kMaxSendQueueDepth = 8192U;
constexpr std::uint32_t kDefaultMaxWrBytes = 64U * 1024U;
constexpr std::uint32_t kDefaultMaxWrsPerWindow = 64U;
constexpr std::uint32_t kCompletionTimeoutMs = 5000U;
constexpr std::uint64_t kLocalOffsetSeed = UINT64_C(0);
constexpr std::uint64_t kRemoteOffsetSeed = UINT64_C(0x9e3779b97f4a7c15);

enum class Role {
    Server,
    Client,
};

struct Config {
    Role role{Role::Client};
    nds::client::RuntimeConfig runtime;
    nds::client::TransportConfig transport;
    nds::client::ExecutionConfig execution;
    nds::benchmark::Operation operation{nds::benchmark::Operation::Read};
    std::uint32_t bytes{};
    std::uint64_t mr_bytes{};
    std::uint32_t in_flight{kDefaultInFlight};
    std::uint32_t qps{1U};
    std::uint32_t warmup{100U};
    std::uint32_t iterations{1000U};
    std::uint32_t wr_per_doorbell{1U};
    std::uint32_t wr_per_signal{1U};
    std::uint32_t max_wr_bytes{kDefaultMaxWrBytes};
    std::uint32_t max_wrs_per_window{kDefaultMaxWrsPerWindow};
    std::uint32_t send_queue_depth{kDefaultSendQueueDepth};
    bool ring_last{};
    bool random_addresses{};
    bool independent_random_addresses{};
    bool verify{true};
    bool aicpu{};
    bool aiv{};
    bool aicpu_device_loop{true};
    bool aicpu_linked_wrs{};
    std::uint32_t aicpu_poll_batch{1U};
    std::uint32_t aicpu_signal_every{};
    std::uint32_t aicpu_linked_wrs_count{16U};
    std::string aicpu_runner{"device-loop"};
    std::string aicpu_qp_mode{"normal"};
    std::string aiv_qp_mode{"normal"};
};

nds::Result<Config> parse(int argc, char **argv) {
    Config config;
    std::string role{"client"};
    std::string operation{"read"};
    std::string backend{"ra"};
    std::string aicpu_runner{"device-loop"};
    bool no_verify{};
    CLI::App app{"Temporary NPU-to-NPU verbs benchmark over RoCE."};
    app.add_option("--role", role)->required()->check(CLI::IsMember({"server", "client"}));
    app.add_option("--ascendcl", config.runtime.ascendcl_library)->required();
    app.add_option("--runtime", config.runtime.runtime_library)->required();
    app.add_option("--ra", config.transport.endpoint.ra_library)->required();
    app.add_option("--aicpu-kernel-config", config.execution.aicpu_kernel_config,
                   "Generated or CP1-overlay AICPU package configuration.");
    app.add_option("--aicpu-qp-mode", config.aicpu_qp_mode,
                   "AI-QP mode for AICPU benchmark experimentation.")
        ->check(CLI::IsMember({"normal", "opbase-ext"}));
    app.add_option("--aiv-kernel", config.execution.aiv_kernel, "Generated AIV kernel object.");
    app.add_option("--aiv-qp-mode", config.aiv_qp_mode,
                   "AI-QP mode for AIV: NORMAL is HCOMM's direct-RoCE mode.")
        ->check(CLI::IsMember({"normal", "opbase-ext"}));
    app.add_option("--logical-device", config.runtime.logical_device_id)->required();
    app.add_option("--listen", config.transport.listen_address, "Server control address.");
    app.add_option("--server", config.transport.server_address, "Client control address.");
    app.add_option("--operation", operation)->required()->check(CLI::IsMember({"read", "write"}));
    app.add_option("--backend", backend, "Data-plane backend.")->check(CLI::IsMember({"ra", "aicpu", "aiv"}));
    app.add_option("--aicpu-runner", aicpu_runner, "AICPU submission runner.")
        ->check(CLI::IsMember({"device-loop", "host-operators"}));
    app.add_flag("--aicpu-linked-wrs", config.aicpu_linked_wrs,
                 "Use one linked provider WR list per AICPU post batch.");
    app.add_option("--aicpu-linked-wrs-count", config.aicpu_linked_wrs_count,
                   "Number of WRs per linked AICPU provider post.")
        ->check(CLI::Range(1U, 64U));
    app.add_option("--aicpu-poll-batch", config.aicpu_poll_batch,
                   "Maximum CQEs requested by each AICPU poll call.")
        ->check(CLI::Range(1U, NDS_DEVICE_MAX_COMPLETIONS));
    app.add_option("--aicpu-signal-every", config.aicpu_signal_every,
                   "Signal every N AICPU WQEs; zero signals only each window boundary.")
        ->check(CLI::Range(0U, static_cast<std::uint32_t>(UINT8_MAX)));
    app.add_option("--bytes", config.bytes)->required()->check(CLI::Range(1U, UINT32_MAX));
    app.add_option("--mr-bytes", config.mr_bytes, "Registered MR size per QP; zero selects the minimum.")
        ->check(CLI::Range(std::uint64_t{0U}, std::numeric_limits<std::uint64_t>::max()));
    app.add_option("--in-flight", config.in_flight)->check(CLI::Range(1U, kMaxInFlight));
    app.add_option("--qps", config.qps)->check(CLI::Range(1U, kMaxQps));
    app.add_option("--warmup", config.warmup)->check(CLI::Range(0U, UINT32_MAX));
    app.add_option("--iterations", config.iterations)->check(CLI::Range(1U, UINT32_MAX));
    app.add_option("--path-mtu", config.transport.qp.path_mtu)
        ->check(CLI::IsMember({256U, 512U, 1024U, 2048U, 4096U}));
    app.add_option("--wr-per-doorbell", config.wr_per_doorbell,
                   "Prepare this many WQEs before ringing the doorbell.")
        ->check(CLI::Range(1U, std::numeric_limits<std::uint32_t>::max()));
    app.add_option("--max-wr-bytes", config.max_wr_bytes)->check(CLI::Range(0U, UINT32_MAX));
    app.add_option("--max-wrs-per-window", config.max_wrs_per_window)->check(CLI::Range(1U, UINT32_MAX));
    app.add_option("--send-queue-depth", config.send_queue_depth,
                   "Send queue depth per QP; must cover one submitted WQE window.")
        ->check(CLI::Range(1U, kMaxSendQueueDepth));
    app.add_option("--wr-per-signal", config.wr_per_signal,
                   "Signal every this many WQEs for CQ completion.")
        ->check(CLI::Range(1U, std::numeric_limits<std::uint32_t>::max()));
    app.add_flag("--ring-last", config.ring_last,
                 "Legacy shorthand: set both batching intervals to the window size.");
    app.add_flag("--random-addresses", config.random_addresses,
                 "Use unique reproducible non-overlapping offsets within each MR.");
    app.add_flag("--independent-random-addresses", config.independent_random_addresses,
                 "Use different random unique local and remote address permutations for AICPU Writes.");
    app.add_flag("--no-verify", no_verify, "Skip post-completion destination verification.");
    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError &error) {
        return nds::unexpected(nds::ErrorCode::kInvalidArgument,
                               app.exit(error) == 0 ? "help requested" : "invalid options");
    }
    if (config.ring_last) {
        if (app.count("--wr-per-doorbell") == 0U)
            config.wr_per_doorbell = config.max_wrs_per_window;
        if (app.count("--wr-per-signal") == 0U)
            config.wr_per_signal = config.max_wrs_per_window;
    }
    if (config.max_wrs_per_window > config.send_queue_depth)
        return nds::unexpected(nds::ErrorCode::kInvalidArgument,
                               "max WQEs per window exceeds the send queue depth");
    if ((config.send_queue_depth & (config.send_queue_depth - 1U)) != 0U)
        return nds::unexpected(nds::ErrorCode::kInvalidArgument,
                               "send queue depth must be a power of two");
    config.role = role == "server" ? Role::Server : Role::Client;
    config.operation = operation == "read" ? nds::benchmark::Operation::Read : nds::benchmark::Operation::Write;
    config.aicpu = backend == "aicpu";
    config.aiv = backend == "aiv";
    config.aicpu_device_loop = aicpu_runner == "device-loop";
    config.aicpu_runner = aicpu_runner;
    config.verify = !no_verify;
    if (config.aicpu && config.aicpu_qp_mode == "opbase-ext" &&
        (config.aicpu_device_loop || config.operation != nds::benchmark::Operation::Write)) {
        return nds::unexpected(nds::ErrorCode::kInvalidArgument,
                               "AICPU OPBASE_EXT diagnostic requires --aicpu-runner host-operators and --operation write");
    }
    if (config.independent_random_addresses &&
        (!config.random_addresses || config.operation != nds::benchmark::Operation::Write ||
         (config.role == Role::Client && (!config.aicpu || !config.aicpu_device_loop)))) {
        return nds::unexpected(nds::ErrorCode::kInvalidArgument,
                               "independent random addresses require an AICPU device-loop Write with random addresses");
    }
    config.transport.qp_count = config.qps;
    config.transport.qp.send_queue_depth = config.send_queue_depth;
    config.transport.qp.receive_queue_depth = 128U;
    config.transport.tcp_timeout_ms = kCompletionTimeoutMs;
    if (config.aicpu) {
        config.execution.mode = nds::client::NpuExecutionMode::Aicpu;
        config.transport.qp.control_flags |= nds::client::QueuePairCallerPollsCq;
        config.transport.qp.ai_qp_mode = config.aicpu_qp_mode == "normal" ? NDS_RA_QP_MODE_NORMAL
                                                                             : NDS_RA_QP_MODE_OPBASE_EXT;
    }
    if (config.aiv) {
        config.execution.mode = nds::client::NpuExecutionMode::Aiv;
        config.transport.qp.control_flags |= nds::client::QueuePairCallerPollsCq;
        config.transport.qp.ai_qp_mode = config.aiv_qp_mode == "normal" ? NDS_RA_QP_MODE_NORMAL
                                                                           : NDS_RA_QP_MODE_OPBASE_EXT;
        if (config.wr_per_doorbell > UINT8_MAX || config.wr_per_signal > UINT8_MAX)
            return nds::unexpected(nds::ErrorCode::kInvalidArgument,
                                   "AIV doorbell and signal intervals must be at most 255 WQEs");
    }
    if (config.aicpu && config.role == Role::Client && config.execution.aicpu_kernel_config.empty())
        return nds::unexpected(nds::ErrorCode::kInvalidArgument,
                               "AICPU client requires --aicpu-kernel-config");
    if (config.aiv && config.role == Role::Client && config.execution.aiv_kernel.empty())
        return nds::unexpected(nds::ErrorCode::kInvalidArgument, "AIV client requires --aiv-kernel");
    if (config.role == Role::Server && config.transport.listen_address.empty())
        return nds::unexpected(nds::ErrorCode::kInvalidArgument, "server role requires --listen");
    if (config.role == Role::Client && config.transport.server_address.empty())
        return nds::unexpected(nds::ErrorCode::kInvalidArgument, "client role requires --server");
    return config;
}

nds::Result<std::size_t> total_bytes(const Config &config) {
    const std::uint64_t operation_count = static_cast<std::uint64_t>(config.warmup) + config.iterations;
    if (operation_count == 0U || config.bytes > std::numeric_limits<std::size_t>::max() / operation_count)
        return nds::unexpected(nds::ErrorCode::kInvalidArgument, "benchmark buffer size overflows address space");
    const std::size_t minimum = static_cast<std::size_t>(config.bytes) * static_cast<std::size_t>(operation_count);
    if (config.mr_bytes == 0U)
        return minimum;
    if (config.mr_bytes < config.bytes || config.mr_bytes > std::numeric_limits<std::size_t>::max())
        return nds::unexpected(nds::ErrorCode::kInvalidArgument, "requested MR size is invalid");
    return std::max(minimum, static_cast<std::size_t>(config.mr_bytes));
}

nds::Result<std::vector<std::uint64_t>> make_offsets(const Config &config, std::size_t mr_bytes,
                                                     std::uint32_t qp_index, std::uint64_t seed) {
    const std::uint64_t operation_count = static_cast<std::uint64_t>(config.warmup) + config.iterations;
    std::vector<std::uint64_t> offsets(static_cast<std::size_t>(operation_count));
    if (!config.random_addresses) {
        for (std::uint64_t index = 0U; index < operation_count; ++index)
            offsets[static_cast<std::size_t>(index)] = index * config.bytes;
        return offsets;
    }
    const std::uint64_t kAlignment = config.bytes;
    const std::uint64_t slot_count = (mr_bytes - config.bytes) / kAlignment + 1U;
    if (operation_count > slot_count)
        return nds::unexpected(nds::ErrorCode::kInvalidArgument,
                               "MR does not contain enough aligned unique random slots");
    std::vector<std::uint64_t> slots(static_cast<std::size_t>(slot_count));
    std::iota(slots.begin(), slots.end(), 0U);
    std::mt19937_64 generator(UINT64_C(0x4e44535f52414e) + qp_index + seed);
    for (std::uint64_t index = 0U; index < operation_count; ++index) {
        const std::uint64_t remaining = slot_count - index;
        const std::uint64_t selected = index + generator() % remaining;
        std::swap(slots[static_cast<std::size_t>(index)], slots[static_cast<std::size_t>(selected)]);
        offsets[static_cast<std::size_t>(index)] = slots[static_cast<std::size_t>(index)] * kAlignment;
    }
    return offsets;
}

nds::Result<void> check_completions(nds::client::QueuePair *qp, std::uint32_t expected, std::mutex *poll_mutex) {
    if (poll_mutex == nullptr)
        return nds::unexpected(nds::ErrorCode::kInvalidArgument, "CQ poll requires a mutex");
    // The target RA/CQ implementation requires one host poller to drain a
    // QP's completion sequence before another QP is polled. HCCL's internal
    // poller has this ownership; this temporary benchmark makes it explicit.
    std::lock_guard poll_lock(*poll_mutex);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kCompletionTimeoutMs);
    NdsDeviceWc completions[NDS_DEVICE_MAX_COMPLETIONS]{};
    std::uint32_t completed{};
    while (completed < expected && std::chrono::steady_clock::now() < deadline) {
        const std::uint32_t requested = std::min(expected - completed, NDS_DEVICE_MAX_COMPLETIONS);
        nds::Result<std::uint32_t> polled;
        polled = nds::NdsRaPollCq(qp, true, requested, completions);
        if (!polled)
            return nds::unexpected(polled.error());
        for (std::uint32_t index = 0U; index < *polled; ++index) {
            if (completions[index].status != NDS_RA_WC_SUCCESS)
                return nds::unexpected(nds::ErrorCode::kRa,
                                       "NPU send CQE failed: status=" + std::to_string(completions[index].status) +
                                           ", vendor_error=" + std::to_string(completions[index].vendor_error));
        }
        completed += *polled;
    }
    if (completed != expected) {
        std::string detail = "timed out waiting for NPU send CQEs: " + std::to_string(completed) + "/" +
                             std::to_string(expected);
        const auto qp_status = qp->query_status();
        if (qp_status)
            detail += ", qp_status=" + std::to_string(*qp_status);
        else
            detail += ", qp_status_query=" + qp_status.error().message;
        const auto errors = qp->query_cqe_errors();
        if (errors) {
            detail += ", cqe_errors=" + std::to_string(errors->size());
            if (!errors->empty())
                detail += ", last_cqe_status=" + std::to_string(errors->back().status) +
                          ", last_cqe_qp=" + std::to_string(errors->back().qp_number);
        } else {
            detail += ", cqe_error_query=" + errors.error().message;
        }
        return nds::unexpected(nds::ErrorCode::kRa, detail);
    }
    return {};
}

nds::Result<void> transfer_window(nds::client::Runtime *runtime, nds::client::QueuePair *qp,
                                  const nds::client::MemoryRegion &local,
                                  const nds::benchmark::RemoteMemory &remote, const Config &config,
                                  const std::vector<std::uint64_t> &offsets,
                                  std::uint64_t first_request, std::uint32_t request_count,
                                  std::uint64_t *next_wr_id, std::mutex *poll_mutex) {
    if (runtime == nullptr || qp == nullptr || next_wr_id == nullptr || request_count == 0U ||
        poll_mutex == nullptr || first_request + request_count > offsets.size())
        return nds::unexpected(nds::ErrorCode::kInvalidArgument, "invalid benchmark transfer window");
    const std::uint32_t chunk_bytes =
        config.max_wr_bytes == 0U ? config.bytes : std::min(config.bytes, config.max_wr_bytes);
    const std::uint64_t chunks_per_request =
        (static_cast<std::uint64_t>(config.bytes) + chunk_bytes - 1U) / chunk_bytes;
    const std::uint64_t total_wrs = static_cast<std::uint64_t>(request_count) * chunks_per_request;
    if (total_wrs == 0U || total_wrs > UINT32_MAX)
        return nds::unexpected(nds::ErrorCode::kInvalidArgument, "benchmark WQE count is invalid");
    const std::uint32_t doorbell_group_size = config.wr_per_doorbell;
    const std::uint32_t signal_group_size = config.wr_per_signal;
    std::vector<NdsRaSge> sges(static_cast<std::size_t>(total_wrs));
    NdsRaSendResponse last_response{};
    std::uint32_t expected_completions{};
    std::uint64_t wr_index{};
    for (std::uint32_t request = 0U; request < request_count; ++request) {
        const std::uint64_t base_offset = offsets[static_cast<std::size_t>(first_request + request)];
        for (std::uint64_t chunk_offset = 0U; chunk_offset < config.bytes; chunk_offset += chunk_bytes) {
            const std::uint32_t length =
                std::min(chunk_bytes, static_cast<std::uint32_t>(config.bytes - chunk_offset));
            const bool doorbell_boundary =
                wr_index + 1U == total_wrs || (wr_index + 1U) % doorbell_group_size == 0U;
            const bool signal_boundary =
                wr_index + 1U == total_wrs || (wr_index + 1U) % signal_group_size == 0U;
            const NdsDeviceSendWr wr{
                (*next_wr_id)++, remote.operation == nds::benchmark::Operation::Read
                                         ? NDS_DEVICE_WR_RDMA_READ
                                         : NDS_DEVICE_WR_RDMA_WRITE,
                signal_boundary ? static_cast<std::uint32_t>(NDS_DEVICE_SEND_SIGNALED) : 0U,
                {local.address() + base_offset + chunk_offset, length, local.local_key()},
                remote.address + base_offset + chunk_offset, remote.remote_key, 0U};
            if (doorbell_group_size == 1U) {
                if (const auto posted = nds::NdsRaPostSend(runtime, qp, wr); !posted)
                    return nds::unexpected(posted.error());
            } else {
                const auto prepared = nds::NdsRaPrepareSend(qp, wr, &sges[wr_index]);
                if (!prepared)
                    return nds::unexpected(prepared.error());
                last_response = *prepared;
                if (doorbell_boundary) {
                    if (const auto rung = nds::NdsRaRingSend(runtime, last_response); !rung)
                        return nds::unexpected(rung.error());
                }
            }
            if (signal_boundary)
                ++expected_completions;
            ++wr_index;
        }
    }
    return check_completions(qp, expected_completions, poll_mutex);
}

nds::Result<void> transfer_operations(nds::client::Runtime *runtime, nds::client::QueuePair *qp,
                                      const nds::client::MemoryRegion &local,
                                      const nds::benchmark::RemoteMemory &remote, const Config &config,
                                      const std::vector<std::uint64_t> &offsets,
                                      std::uint64_t first_request, std::uint32_t operations,
                                      std::uint64_t *next_wr_id, std::mutex *poll_mutex) {
    const std::uint32_t chunk_bytes =
        config.max_wr_bytes == 0U ? config.bytes : std::min(config.bytes, config.max_wr_bytes);
    const std::uint64_t wrs_per_request =
        (static_cast<std::uint64_t>(config.bytes) + chunk_bytes - 1U) / chunk_bytes;
    if (wrs_per_request > config.max_wrs_per_window)
        return nds::unexpected(nds::ErrorCode::kInvalidArgument,
                               "max WQEs per window is smaller than one logical transfer");
    const std::uint32_t requests_per_window = static_cast<std::uint32_t>(
        std::min<std::uint64_t>(config.in_flight, config.max_wrs_per_window / wrs_per_request));
    for (std::uint32_t completed = 0U; completed < operations;) {
        const std::uint32_t window = std::min(requests_per_window, operations - completed);
        if (const auto transferred = transfer_window(runtime, qp, local, remote, config, offsets,
                                                     first_request + completed, window, next_wr_id, poll_mutex);
            !transferred)
            return nds::unexpected(transferred.error());
        completed += window;
    }
    return {};
}

[[maybe_unused]] nds::Result<void> poll_aicpu_completions(nds::AicpuEntrypointLauncher *launcher,
                                         nds::client::Runtime *runtime, const NdsDeviceQp &qp, void *device_wc,
                                         std::uint32_t expected) {
    if (launcher == nullptr || runtime == nullptr || device_wc == nullptr || expected == 0U)
        return nds::unexpected(nds::ErrorCode::kInvalidArgument, "invalid AICPU completion polling state");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kCompletionTimeoutMs);
    std::uint32_t completed{};
    while (completed < expected && std::chrono::steady_clock::now() < deadline) {
        NdsDeviceWc marker{};
        marker.status = std::numeric_limits<std::int32_t>::min();
        if (const auto copied = runtime->copy_host_to_device(device_wc, &marker, sizeof(marker)); !copied)
            return nds::unexpected(copied.error());
        NdsDevicePollCqArgs args{};
        args.qp = qp;
        args.is_send_cq = 1U;
        args.max_completions = 1U;
        args.wc_address = reinterpret_cast<std::uint64_t>(device_wc);
        args.return_value = std::numeric_limits<std::int32_t>::min();
        if (const auto launched = launcher->launch_poll_cq_and_wait(&args, kCompletionTimeoutMs); !launched)
            return nds::unexpected(launched.error());
        NdsDeviceWc observed{};
        if (const auto copied = runtime->copy_device_to_host(&observed, device_wc, sizeof(observed)); !copied)
            return nds::unexpected(copied.error());
        if (observed.status == std::numeric_limits<std::int32_t>::min())
            continue;
        if (observed.status != 0)
            return nds::unexpected(nds::ErrorCode::kRa,
                                   "AICPU send completion failed: status=" + std::to_string(observed.status) +
                                       ", vendor_error=" + std::to_string(observed.vendor_error));
        ++completed;
    }
    if (completed != expected)
        return nds::unexpected(nds::ErrorCode::kRa,
                               "timed out waiting for AICPU send CQEs: " + std::to_string(completed) + "/" +
                                   std::to_string(expected));
    return {};
}

nds::Result<void> transfer_aicpu_window(nds::AicpuEntrypointLauncher *launcher, nds::client::Runtime *runtime,
                                        nds::client::QueuePair *qp, const nds::client::MemoryRegion &local,
                                        const nds::benchmark::RemoteMemory &remote, const Config &config,
                                        const std::vector<std::uint64_t> &offsets, std::uint64_t first_request,
                                        std::uint32_t request_count, std::uint64_t *next_wr_id, void *device_result,
                                        void *device_offsets, void *device_remote_offsets) {
    if (launcher == nullptr || runtime == nullptr || qp == nullptr || next_wr_id == nullptr || device_result == nullptr ||
        request_count == 0U || first_request + request_count > offsets.size())
        return nds::unexpected(nds::ErrorCode::kInvalidArgument, "invalid AICPU transfer window");
    const auto device_transport = qp->make_device_transport();
    if (!device_transport)
        return nds::unexpected(device_transport.error());
    const std::uint64_t base_offset = offsets[static_cast<std::size_t>(first_request)];
    if (config.aicpu_device_loop) {
        NdsDeviceRdmaBenchmarkArgs args{};
        args.transport = *device_transport;
        args.local_address = local.address() + (config.random_addresses ? 0U : base_offset);
        args.remote_address = remote.address + (config.random_addresses ? 0U : base_offset);
        args.local_key = local.local_key();
        args.remote_key = remote.remote_key;
        args.bytes = config.bytes;
        args.iterations = request_count;
        args.in_flight = std::min(config.in_flight, request_count);
        args.max_wr_bytes = config.max_wr_bytes;
        args.max_wrs_per_window = config.max_wrs_per_window;
        args.post_mode = (config.aicpu_linked_wrs ? NDS_DEVICE_BENCHMARK_POST_LINKED
                                                  : NDS_DEVICE_BENCHMARK_POST_INDIVIDUAL) |
                         (config.aicpu_poll_batch << NDS_DEVICE_BENCHMARK_POLL_BATCH_SHIFT) |
                         (config.aicpu_signal_every << NDS_DEVICE_BENCHMARK_SIGNAL_EVERY_SHIFT) |
                         (config.aicpu_linked_wrs_count << NDS_DEVICE_BENCHMARK_LINKED_WRS_SHIFT);
        args.operation = remote.operation == nds::benchmark::Operation::Read ? NDS_DEVICE_BENCHMARK_READ
                                                                               : NDS_DEVICE_BENCHMARK_WRITE;
        args.result_address = reinterpret_cast<std::uint64_t>(device_result);
        args.request_offsets_address = config.random_addresses ? reinterpret_cast<std::uint64_t>(device_offsets) : 0U;
        args.remote_request_offsets_address =
            config.independent_random_addresses ? reinterpret_cast<std::uint64_t>(device_remote_offsets) : 0U;
        args.request_offset_start = first_request;
        args.return_value = std::numeric_limits<std::int32_t>::min();
        if (const auto launched = launcher->launch_rdma_benchmark_and_wait(&args, kCompletionTimeoutMs); !launched)
            return nds::unexpected(launched.error());
        NdsDeviceRdmaBenchmarkResult result{};
        if (const auto copied = runtime->copy_device_to_host(&result, device_result, sizeof(result)); !copied)
            return nds::unexpected(copied.error());
        if (result.status != NDS_DEVICE_BENCHMARK_SUCCESS || result.operations_completed != request_count)
            return nds::unexpected(nds::ErrorCode::kRa,
                                   "AICPU device-loop failed: status=" + std::to_string(result.status) +
                                       ", operations=" + std::to_string(result.operations_completed) + "/" +
                                       std::to_string(request_count) + ", completion_status=" +
                                       std::to_string(result.completion_status) + ", vendor_error=" +
                                       std::to_string(result.completion_vendor_error));
        *next_wr_id += result.wqe_count;
        return {};
    }
    const std::uint32_t chunk_bytes =
        config.max_wr_bytes == 0U ? config.bytes : std::min(config.bytes, config.max_wr_bytes);
    const std::uint64_t chunks_per_request =
        (static_cast<std::uint64_t>(config.bytes) + chunk_bytes - 1U) / chunk_bytes;
    const std::uint64_t total_wrs = static_cast<std::uint64_t>(request_count) * chunks_per_request;
    if (total_wrs == 0U || total_wrs > UINT32_MAX)
        return nds::unexpected(nds::ErrorCode::kInvalidArgument, "AICPU WQE count is invalid");
    for (std::uint32_t request = 0U; request < request_count; ++request) {
        const std::uint64_t request_offset = base_offset + static_cast<std::uint64_t>(request) * config.bytes;
        for (std::uint64_t chunk_offset = 0U; chunk_offset < config.bytes; chunk_offset += chunk_bytes) {
            const std::uint32_t length =
                std::min(chunk_bytes, static_cast<std::uint32_t>(config.bytes - chunk_offset));
            const NdsDeviceSendWr wr{(*next_wr_id)++, remote.operation == nds::benchmark::Operation::Read
                                                          ? NDS_DEVICE_WR_RDMA_READ
                                                          : NDS_DEVICE_WR_RDMA_WRITE,
                                     static_cast<std::uint32_t>(NDS_DEVICE_SEND_SIGNALED),
                                     {local.address() + request_offset + chunk_offset, length, local.local_key()},
                                     remote.address + request_offset + chunk_offset, remote.remote_key, 0U};
            if (remote.operation == nds::benchmark::Operation::Read) {
                NdsDeviceRdmaReadArgs args{*device_transport, wr, std::numeric_limits<std::int32_t>::min(), 0U};
                if (const auto launched = launcher->launch_rdma_read_and_wait(&args, kCompletionTimeoutMs); !launched)
                    return nds::unexpected(launched.error());
            } else {
                NdsDevicePostSendArgs args{};
                args.qp = device_transport->control_qp;
                args.wr = wr;
                args.return_value = std::numeric_limits<std::int32_t>::min();
                args.doorbell_address = reinterpret_cast<std::uint64_t>(device_result);
                if (const auto launched = launcher->launch_post_send_and_wait(&args, kCompletionTimeoutMs); !launched)
                    return nds::unexpected(launched.error());
                if (device_transport->control_qp.qp_mode == NDS_DEVICE_QP_MODE_OPBASE_EXT) {
                    NdsDeviceDoorbell doorbell{};
                    if (const auto copied = runtime->copy_device_to_host(&doorbell, device_result, sizeof(doorbell));
                        !copied)
                        return nds::unexpected(copied.error());
                    if (doorbell.reserved != 0U)
                        return nds::unexpected(nds::ErrorCode::kRa,
                                               "AICPU provider post failed before doorbell: " +
                                                   std::to_string(static_cast<std::int32_t>(doorbell.reserved)));
                    NdsRaSendResponse response{};
                    response.doorbell.db_index = doorbell.index;
                    response.doorbell.db_info = doorbell.info;
                    if (const auto rung = nds::NdsRaRingSend(runtime, response); !rung)
                        return nds::unexpected(rung.error());
                }
            }
        }
    }
    return poll_aicpu_completions(launcher, runtime, device_transport->control_qp, device_result,
                                  static_cast<std::uint32_t>(total_wrs));
}

nds::Result<void> transfer_aicpu_operations(nds::AicpuEntrypointLauncher *launcher, nds::client::Runtime *runtime,
                                            nds::client::QueuePair *qp, const nds::client::MemoryRegion &local,
                                            const nds::benchmark::RemoteMemory &remote, const Config &config,
                                            const std::vector<std::uint64_t> &offsets, std::uint64_t first_request,
                                            std::uint32_t operations, std::uint64_t *next_wr_id, void *device_wc,
                                            void *device_offsets, void *device_remote_offsets) {
    const std::uint32_t chunk_bytes =
        config.max_wr_bytes == 0U ? config.bytes : std::min(config.bytes, config.max_wr_bytes);
    const std::uint64_t wrs_per_request =
        (static_cast<std::uint64_t>(config.bytes) + chunk_bytes - 1U) / chunk_bytes;
    if (wrs_per_request > config.max_wrs_per_window)
        return nds::unexpected(nds::ErrorCode::kInvalidArgument,
                               "max WQEs per window is smaller than one logical transfer");
    const std::uint32_t requests_per_window = static_cast<std::uint32_t>(
        std::min<std::uint64_t>(config.in_flight, config.max_wrs_per_window / wrs_per_request));
    for (std::uint32_t completed = 0U; completed < operations;) {
        const std::uint32_t window = std::min(requests_per_window, operations - completed);
        if (const auto transferred = transfer_aicpu_window(launcher, runtime, qp, local, remote, config, offsets,
                                                            first_request + completed, window, next_wr_id, device_wc,
                                                            device_offsets, device_remote_offsets);
            !transferred)
            return nds::unexpected(transferred.error());
        completed += window;
    }
    return {};
}

nds::Result<void> transfer_aiv_operations(nds::AivEntrypointLauncher *launcher, nds::client::Runtime *runtime,
                                          nds::client::QueuePair *qp, const nds::client::MemoryRegion &local,
                                          const nds::benchmark::RemoteMemory &remote, const Config &config,
                                          const std::vector<std::uint64_t> &offsets, std::uint64_t first_request,
                                          std::uint32_t operations, void *device_args, void *device_result,
                                          void *device_offsets) {
    if (launcher == nullptr || runtime == nullptr || qp == nullptr || device_args == nullptr || device_result == nullptr ||
        (config.random_addresses && device_offsets == nullptr))
        return nds::unexpected(nds::ErrorCode::kInvalidArgument,
                               "AIV peer benchmark requires a device random-offset table");
    const auto device_transport = qp->make_device_transport();
    if (!device_transport)
        return nds::unexpected(device_transport.error());
    const std::uint32_t chunk_bytes =
        config.max_wr_bytes == 0U ? config.bytes : std::min(config.bytes, config.max_wr_bytes);
    const std::uint64_t wrs_per_request =
        (static_cast<std::uint64_t>(config.bytes) + chunk_bytes - 1U) / chunk_bytes;
    if (wrs_per_request > config.max_wrs_per_window)
        return nds::unexpected(nds::ErrorCode::kInvalidArgument,
                               "max WQEs per window is smaller than one logical transfer");
    const std::uint32_t requests_per_window = static_cast<std::uint32_t>(
        std::min<std::uint64_t>(config.in_flight, config.max_wrs_per_window / wrs_per_request));
    for (std::uint32_t completed = 0U; completed < operations;) {
        const std::uint32_t window = std::min(requests_per_window, operations - completed);
        const std::uint64_t offset = offsets[static_cast<std::size_t>(first_request + completed)];
        NdsDeviceRdmaBenchmarkArgs args{};
        args.transport = *device_transport;
        args.local_address = local.address() + (config.random_addresses ? 0U : offset);
        args.remote_address = remote.address + (config.random_addresses ? 0U : offset);
        args.local_key = local.local_key();
        args.remote_key = remote.remote_key;
        args.bytes = config.bytes;
        args.iterations = window;
        args.in_flight = window;
        args.max_wr_bytes = config.max_wr_bytes;
        args.max_wrs_per_window = config.max_wrs_per_window;
        args.post_mode = config.wr_per_doorbell |
                         (config.wr_per_signal << NDS_DEVICE_BENCHMARK_SIGNAL_EVERY_SHIFT);
        args.operation = remote.operation == nds::benchmark::Operation::Read ? NDS_DEVICE_BENCHMARK_READ
                                                                               : NDS_DEVICE_BENCHMARK_WRITE;
        args.result_address = reinterpret_cast<std::uint64_t>(device_result);
        args.request_offsets_address =
            config.random_addresses ? reinterpret_cast<std::uint64_t>(device_offsets) : 0U;
        args.request_offset_start = first_request + completed;
        args.return_value = std::numeric_limits<std::int32_t>::min();
        if (const auto copied = runtime->copy_host_to_device(device_args, &args, sizeof(args)); !copied)
            return nds::unexpected(copied.error());
        if (const auto launched = launcher->launch_rdma_benchmark_and_wait(reinterpret_cast<std::uint64_t>(device_args),
                                                                            kCompletionTimeoutMs);
            !launched)
            return nds::unexpected(launched.error());
        NdsDeviceRdmaBenchmarkResult result{};
        if (const auto copied = runtime->copy_device_to_host(&result, device_result, sizeof(result)); !copied)
            return nds::unexpected(copied.error());
        if (result.status != NDS_DEVICE_BENCHMARK_SUCCESS || result.operations_completed != window)
            return nds::unexpected(nds::ErrorCode::kRa,
                                   "AIV device-loop failed: status=" + std::to_string(result.status) +
                                       ", operations=" + std::to_string(result.operations_completed) + "/" +
                                       std::to_string(window) +
                                       ", completion_status=" + std::to_string(result.completion_status) +
                                       ", vendor_error=" + std::to_string(result.completion_vendor_error));
        completed += window;
    }
    return {};
}

nds::Result<void> exchange_memory_records(nds::TcpPeerExchange *control,
                                          const std::vector<nds::client::MemoryRegion> &local,
                                          nds::benchmark::Operation operation, bool server,
                                          std::vector<nds::benchmark::RemoteMemory> *remote) {
    if (control == nullptr || remote == nullptr || local.empty())
        return nds::unexpected(nds::ErrorCode::kInvalidArgument, "invalid NPU MR exchange");
    std::vector<std::uint8_t> local_bytes(local.size() * nds::benchmark::kMemoryRecordBytes);
    for (std::size_t index = 0U; index < local.size(); ++index) {
        std::array<std::uint8_t, nds::benchmark::kMemoryRecordBytes> record{};
        if (!nds::benchmark::serialize_remote_memory(
                {operation, local[index].address(), local[index].length(), local[index].remote_key()}, &record))
            return nds::unexpected(nds::ErrorCode::kTransport, "cannot serialize NPU MR metadata");
        std::copy(record.begin(), record.end(), local_bytes.begin() + index * record.size());
    }
    std::vector<std::uint8_t> peer_bytes(local_bytes.size());
    if (server) {
        if (const auto sent = control->send_bytes(local_bytes.data(), local_bytes.size()); !sent)
            return nds::unexpected(sent.error());
        if (const auto received = control->receive_bytes(peer_bytes.data(), peer_bytes.size()); !received)
            return nds::unexpected(received.error());
    } else {
        if (const auto received = control->receive_bytes(peer_bytes.data(), peer_bytes.size()); !received)
            return nds::unexpected(received.error());
        if (const auto sent = control->send_bytes(local_bytes.data(), local_bytes.size()); !sent)
            return nds::unexpected(sent.error());
    }
    remote->resize(local.size());
    for (std::size_t index = 0U; index < remote->size(); ++index) {
        std::array<std::uint8_t, nds::benchmark::kMemoryRecordBytes> record{};
        std::copy_n(peer_bytes.begin() + index * record.size(), record.size(), record.begin());
        if (!nds::benchmark::deserialize_remote_memory(record, &(*remote)[index]) ||
            (*remote)[index].operation != operation)
            return nds::unexpected(nds::ErrorCode::kTransport, "invalid peer NPU MR metadata");
    }
    return {};
}

bool verify_ranges(nds::client::Runtime *runtime, const nds::client::MemoryBuffer &buffer,
                   const std::vector<std::uint64_t> &offsets, std::uint32_t bytes, std::uint8_t expected) {
    std::vector<std::byte> host(bytes);
    for (const std::uint64_t offset : offsets) {
        if (const auto copied = runtime->copy_device_to_host(host.data(),
                                                             static_cast<std::byte *>(buffer.rdma_data()) + offset,
                                                             host.size());
            !copied)
            return false;
        if (!std::all_of(host.begin(), host.end(), [expected](std::byte value) {
                return std::to_integer<std::uint8_t>(value) == expected;
            }))
            return false;
    }
    return true;
}

void save_error(std::string *message, std::mutex *mutex, std::atomic_bool *failed, std::uint32_t index,
                const nds::Error &error) {
    std::lock_guard lock(*mutex);
    if (message->empty())
        *message = "QP " + std::to_string(index) + ": " + error.message;
    failed->store(true);
}

int run(const Config &config) {
    const auto memory_bytes = total_bytes(config);
    if (!memory_bytes) {
        NDS_LOG_ERROR("npu-peer", "invalid benchmark buffer size: {}", memory_bytes.error().message);
        return EXIT_FAILURE;
    }
    nds::client::Runtime runtime;
    if (const auto opened = runtime.open(config.runtime); !opened) {
        NDS_LOG_ERROR("npu-peer", "runtime open failed: {}", opened.error().message);
        return EXIT_FAILURE;
    }
    nds::client::Transport transport;
    const auto opened = config.role == Role::Server
                            ? transport.open_server(&runtime, config.transport, config.execution)
                            : transport.open(&runtime, config.transport, config.execution);
    if (!opened) {
        NDS_LOG_ERROR("npu-peer", "NPU transport open failed: {}", opened.error().message);
        return EXIT_FAILURE;
    }

    std::vector<nds::client::MemoryBuffer> buffers;
    std::vector<nds::client::MemoryRegion> regions;
    std::vector<std::vector<std::uint64_t>> offsets;
    std::vector<std::vector<std::uint64_t>> remote_offsets;
    buffers.reserve(config.qps);
    regions.reserve(config.qps);
    offsets.reserve(config.qps);
    remote_offsets.reserve(config.qps);
    const std::uint8_t local_pattern =
        ((config.role == Role::Server && config.operation == nds::benchmark::Operation::Read) ||
         (config.role == Role::Client && config.operation == nds::benchmark::Operation::Write))
            ? 0x5aU
            : 0U;
    std::vector<std::byte> initial(*memory_bytes, static_cast<std::byte>(local_pattern));
    for (std::uint32_t index = 0U; index < config.qps; ++index) {
        const std::uint64_t local_seed =
            config.independent_random_addresses && config.role == Role::Server ? kRemoteOffsetSeed : kLocalOffsetSeed;
        auto qp_offsets = make_offsets(config, *memory_bytes, index, local_seed);
        if (!qp_offsets) {
            NDS_LOG_ERROR("npu-peer", "QP {} address generation failed: {}", index, qp_offsets.error().message);
            return EXIT_FAILURE;
        }
        offsets.push_back(std::move(*qp_offsets));
        if (config.independent_random_addresses && config.role == Role::Client) {
            auto qp_remote_offsets = make_offsets(config, *memory_bytes, index, kRemoteOffsetSeed);
            if (!qp_remote_offsets) {
                NDS_LOG_ERROR("npu-peer", "QP {} remote address generation failed: {}", index,
                              qp_remote_offsets.error().message);
                return EXIT_FAILURE;
            }
            remote_offsets.push_back(std::move(*qp_remote_offsets));
        }
        auto buffer = runtime.allocate(*memory_bytes);
        if (!buffer) {
            NDS_LOG_ERROR("npu-peer", "QP {} HBM allocation failed: {}", index, buffer.error().message);
            return EXIT_FAILURE;
        }
        if (const auto copied = runtime.copy_to(&*buffer, initial.data(), initial.size()); !copied) {
            NDS_LOG_ERROR("npu-peer", "QP {} HBM initialization failed: {}", index, copied.error().message);
            return EXIT_FAILURE;
        }
        buffers.push_back(std::move(*buffer));
        auto region = transport.endpoint()->reg_mr(buffers.back(), nds::client::MemoryAccess::DirectNpu);
        if (!region) {
            NDS_LOG_ERROR("npu-peer", "QP {} HBM registration failed: {}", index, region.error().message);
            return EXIT_FAILURE;
        }
        regions.push_back(std::move(*region));
    }
    std::vector<nds::benchmark::RemoteMemory> remote;
    if (const auto exchanged = exchange_memory_records(transport.bootstrap(), regions, config.operation,
                                                       config.role == Role::Server, &remote);
        !exchanged) {
        NDS_LOG_ERROR("npu-peer", "NPU MR exchange failed: {}", exchanged.error().message);
        return EXIT_FAILURE;
    }
    for (std::size_t index = 0U; index < remote.size(); ++index) {
        if (remote[index].length < *memory_bytes) {
            NDS_LOG_ERROR("npu-peer", "QP {} peer MR is smaller than the requested transfer", index);
            return EXIT_FAILURE;
        }
    }

    if (config.role == Role::Server) {
        std::uint8_t finished{};
        if (const auto received = transport.bootstrap()->receive_bytes(&finished, sizeof(finished));
            !received || finished != 1U) {
            NDS_LOG_ERROR("npu-peer", "client completion handshake failed");
            return EXIT_FAILURE;
        }
        bool verified = true;
        if (config.verify && config.operation == nds::benchmark::Operation::Write) {
            for (std::size_t index = 0U; index < buffers.size(); ++index)
                verified = verified && verify_ranges(&runtime, buffers[index], offsets[index], config.bytes, 0x5aU);
        }
        const std::uint8_t acknowledged = verified ? 1U : 0U;
        (void)transport.bootstrap()->send_bytes(&acknowledged, sizeof(acknowledged));
        if (!verified) {
            NDS_LOG_ERROR("npu-peer", "RDMA Write destination verification failed");
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }

    std::string error_message;
    std::mutex error_mutex;
    std::atomic_bool failed{false};
    std::mutex poll_mutex;
    std::vector<void *> device_results;
    std::vector<void *> device_args;
    std::vector<void *> device_offsets;
    std::vector<void *> device_remote_offsets;
    if (config.aicpu) {
        device_results.reserve(config.qps);
        device_offsets.reserve(config.qps);
        device_remote_offsets.reserve(config.qps);
        for (std::uint32_t index = 0U; index < config.qps; ++index) {
            auto device_result = runtime.allocate_device_memory(sizeof(NdsDeviceRdmaBenchmarkResult));
            if (!device_result) {
                NDS_LOG_ERROR("npu-peer", "QP {} AICPU result allocation failed: {}", index,
                              device_result.error().message);
                return EXIT_FAILURE;
            }
            device_results.push_back(*device_result);
            if (config.random_addresses) {
                auto offset_buffer = runtime.allocate_device_memory(offsets[index].size() * sizeof(offsets[index][0]));
                if (!offset_buffer) {
                    NDS_LOG_ERROR("npu-peer", "QP {} AICPU offset-table allocation failed: {}", index,
                                  offset_buffer.error().message);
                    return EXIT_FAILURE;
                }
                if (const auto copied = runtime.copy_host_to_device(*offset_buffer, offsets[index].data(),
                                                                    offsets[index].size() * sizeof(offsets[index][0]));
                    !copied) {
                    NDS_LOG_ERROR("npu-peer", "QP {} AICPU offset-table copy failed: {}", index,
                                  copied.error().message);
                    return EXIT_FAILURE;
                }
                device_offsets.push_back(*offset_buffer);
                if (config.independent_random_addresses) {
                    auto remote_offset_buffer =
                        runtime.allocate_device_memory(remote_offsets[index].size() * sizeof(remote_offsets[index][0]));
                    if (!remote_offset_buffer) {
                        NDS_LOG_ERROR("npu-peer", "QP {} AICPU remote offset-table allocation failed: {}", index,
                                      remote_offset_buffer.error().message);
                        return EXIT_FAILURE;
                    }
                    if (const auto copied = runtime.copy_host_to_device(
                            *remote_offset_buffer, remote_offsets[index].data(),
                            remote_offsets[index].size() * sizeof(remote_offsets[index][0]));
                        !copied) {
                        NDS_LOG_ERROR("npu-peer", "QP {} AICPU remote offset-table copy failed: {}", index,
                                      copied.error().message);
                        return EXIT_FAILURE;
                    }
                    device_remote_offsets.push_back(*remote_offset_buffer);
                } else {
                    device_remote_offsets.push_back(nullptr);
                }
            } else {
                device_offsets.push_back(nullptr);
                device_remote_offsets.push_back(nullptr);
            }
        }
    }
    if (config.aiv) {
        device_results.reserve(config.qps);
        device_args.reserve(config.qps);
        device_offsets.reserve(config.qps);
        for (std::uint32_t index = 0U; index < config.qps; ++index) {
            auto result = runtime.allocate_device_memory(sizeof(NdsDeviceRdmaBenchmarkResult));
            auto args = runtime.allocate_device_memory(sizeof(NdsDeviceRdmaBenchmarkArgs));
            if (!result || !args) {
                NDS_LOG_ERROR("npu-peer", "QP {} AIV device benchmark allocation failed", index);
                return EXIT_FAILURE;
            }
            device_results.push_back(*result);
            device_args.push_back(*args);
            if (config.random_addresses) {
                auto offset_buffer = runtime.allocate_device_memory(offsets[index].size() * sizeof(offsets[index][0]));
                if (!offset_buffer) {
                    NDS_LOG_ERROR("npu-peer", "QP {} AIV offset-table allocation failed: {}", index,
                                  offset_buffer.error().message);
                    return EXIT_FAILURE;
                }
                if (const auto copied = runtime.copy_host_to_device(*offset_buffer, offsets[index].data(),
                                                                    offsets[index].size() * sizeof(offsets[index][0]));
                    !copied) {
                    NDS_LOG_ERROR("npu-peer", "QP {} AIV offset-table copy failed: {}", index,
                                  copied.error().message);
                    return EXIT_FAILURE;
                }
                device_offsets.push_back(*offset_buffer);
            } else {
                device_offsets.push_back(nullptr);
            }
        }
    }
    std::chrono::steady_clock::time_point start{};
    std::chrono::steady_clock::time_point end{};
    std::barrier start_barrier(static_cast<std::ptrdiff_t>(config.qps + 1U),
                               [&start] { start = std::chrono::steady_clock::now(); });
    std::barrier end_barrier(static_cast<std::ptrdiff_t>(config.qps + 1U),
                             [&end] { end = std::chrono::steady_clock::now(); });
    std::vector<std::thread> workers;
    workers.reserve(config.qps);
    for (std::uint32_t index = 0U; index < config.qps; ++index) {
        workers.emplace_back([&, index] {
            std::uint64_t wr_id = 1U;
            nds::AicpuEntrypointLauncher launcher;
            nds::AivEntrypointLauncher aiv_launcher;
            if (config.aicpu) {
                const int device_result = runtime.acl_api().set_device(
                    static_cast<std::int32_t>(runtime.config().logical_device_id));
                if (device_result != 0) {
                    save_error(&error_message, &error_mutex, &failed, index,
                               nds::Error{nds::ErrorCode::kRuntime,
                                          "aclrtSetDevice in AICPU worker failed: " +
                                              std::to_string(device_result)});
                } else if (const auto loaded = launcher.load(&runtime.acl_api(), config.execution.aicpu_kernel_config);
                           !loaded) {
                    save_error(&error_message, &error_mutex, &failed, index, loaded.error());
                }
            }
            if (config.aiv) {
                const int device_result = runtime.acl_api().set_device(
                    static_cast<std::int32_t>(runtime.config().logical_device_id));
                if (device_result != 0) {
                    save_error(&error_message, &error_mutex, &failed, index,
                               nds::Error{nds::ErrorCode::kRuntime,
                                          "aclrtSetDevice in AIV worker failed: " + std::to_string(device_result)});
                } else if (const auto loaded = aiv_launcher.load(&runtime.acl_api(), config.execution.aiv_kernel); !loaded) {
                    save_error(&error_message, &error_mutex, &failed, index, loaded.error());
                }
            }
            const auto warmup = config.aicpu
                                    ? transfer_aicpu_operations(&launcher, &runtime,
                                                                transport.qp(index), regions[index], remote[index],
                                                                config, offsets[index], 0U, config.warmup, &wr_id,
                                                                device_results[index], device_offsets[index],
                                                                device_remote_offsets[index])
                                    : config.aiv
                                          ? transfer_aiv_operations(&aiv_launcher, &runtime, transport.qp(index),
                                                                    regions[index], remote[index], config, offsets[index],
                                                                    0U, config.warmup, device_args[index], device_results[index],
                                                                    device_offsets[index])
                                          : transfer_operations(&runtime, transport.qp(index), regions[index], remote[index],
                                                                config, offsets[index], 0U, config.warmup, &wr_id,
                                                                &poll_mutex);
            if (!warmup)
                save_error(&error_message, &error_mutex, &failed, index, warmup.error());
            start_barrier.arrive_and_wait();
            if (!failed.load()) {
                const auto measured = config.aicpu
                                          ? transfer_aicpu_operations(
                                                &launcher, &runtime, transport.qp(index), regions[index],
                                                remote[index], config, offsets[index], config.warmup,
                                                config.iterations, &wr_id, device_results[index], device_offsets[index],
                                                device_remote_offsets[index])
                                          : config.aiv
                                                ? transfer_aiv_operations(
                                                      &aiv_launcher, &runtime, transport.qp(index), regions[index],
                                                      remote[index], config, offsets[index], config.warmup,
                                                      config.iterations, device_args[index], device_results[index],
                                                      device_offsets[index])
                                                : transfer_operations(&runtime, transport.qp(index), regions[index],
                                                                      remote[index], config, offsets[index], config.warmup,
                                                                      config.iterations, &wr_id, &poll_mutex);
                if (!measured)
                    save_error(&error_message, &error_mutex, &failed, index, measured.error());
            }
            end_barrier.arrive_and_wait();
        });
    }
    start_barrier.arrive_and_wait();
    end_barrier.arrive_and_wait();
    for (auto &worker : workers)
        worker.join();
    if (!error_message.empty()) {
        NDS_LOG_ERROR("npu-peer", "multi-QP benchmark failed: {}", error_message);
        return EXIT_FAILURE;
    }
    const bool verified = !config.verify || config.operation == nds::benchmark::Operation::Write ||
                          std::all_of(buffers.begin(), buffers.end(), [&, index = std::size_t{0U}](const auto &buffer) mutable {
                              return verify_ranges(&runtime, buffer, offsets[index++], config.bytes, 0x5aU);
                          });
    const std::uint8_t finished = 1U;
    if (const auto sent = transport.bootstrap()->send_bytes(&finished, sizeof(finished)); !sent) {
        NDS_LOG_ERROR("npu-peer", "completion notification failed: {}", sent.error().message);
        return EXIT_FAILURE;
    }
    std::uint8_t acknowledged{};
    if (const auto received = transport.bootstrap()->receive_bytes(&acknowledged, sizeof(acknowledged));
        !received || acknowledged != 1U || !verified) {
        NDS_LOG_ERROR("npu-peer", "peer verification failed");
        return EXIT_FAILURE;
    }
    const double seconds = std::chrono::duration<double>(end - start).count();
    const double operations_per_second = static_cast<double>(config.iterations) * config.qps / seconds;
    const double gib_per_second = static_cast<double>(config.iterations) * config.qps * config.bytes / seconds /
                                  (1024.0 * 1024.0 * 1024.0);
    std::cout << std::fixed << std::setprecision(3)
              << "{\"backend\":\"npu-peer\",\"dataplane\":\"roce\",\"execution\":\""
              << (config.aicpu ? "aicpu-operator" : (config.aiv ? "aiv-operator" : "ra"))
              << "\",\"operation\":\""
              << nds::benchmark::operation_name(config.operation) << "\",\"bytes\":" << config.bytes
              << ",\"aicpu_runner\":\"" << (config.aicpu ? config.aicpu_runner : "none") << "\""
              << ",\"in_flight\":" << config.in_flight << ",\"qps\":" << config.qps
              << ",\"warmup\":" << config.warmup << ",\"iterations\":" << config.iterations
              << ",\"wr_per_doorbell\":" << config.wr_per_doorbell
              << ",\"wr_per_signal\":" << config.wr_per_signal
              << ",\"aicpu_linked_wrs\":" << (config.aicpu_linked_wrs ? "true" : "false")
              << ",\"aicpu_linked_wrs_count\":" << config.aicpu_linked_wrs_count
              << ",\"aicpu_poll_batch\":" << config.aicpu_poll_batch
              << ",\"aicpu_signal_every\":" << config.aicpu_signal_every
              << ",\"aicpu_qp_mode\":\"" << (config.aicpu ? config.aicpu_qp_mode : "none") << "\""
              << ",\"aiv_qp_mode\":\"" << (config.aiv ? config.aiv_qp_mode : "none") << "\""
              << ",\"send_queue_depth\":" << config.send_queue_depth
              << ",\"max_wr_bytes\":" << (config.max_wr_bytes == 0U ? config.bytes : config.max_wr_bytes)
              << ",\"max_wrs_per_window\":" << config.max_wrs_per_window
              << ",\"buffer_bytes_per_qp\":" << *memory_bytes
              << ",\"address_policy\":\""
              << (config.independent_random_addresses ? "random-unique-independent"
                                                       : (config.random_addresses ? "random-unique" : "sequential-unique"))
              << "\","
              << "\"completion_policy\":\""
              << (config.aiv ? (config.wr_per_signal == 1U ? "per-wqe-signaled" : "aiv-group-signaled")
                              : (config.aicpu ? (config.aicpu_device_loop
                                      ? (config.aicpu_signal_every == 0U ? "device-loop-final-signaled"
                                                                          : "device-loop-periodic-signaled")
                                      : "per-wqe-signaled")
                               : "ra-configurable"))
              << "\",\"elapsed_ns\":"
              << std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count()
              << ",\"ops_per_second\":" << operations_per_second << ",\"gib_per_second\":" << gib_per_second
              << "}\n";
    return EXIT_SUCCESS;
}

}  // namespace

int main(int argc, char **argv) {
    (void)nds::log::configure("npu-peer", "stderr", "info");
    const auto config = parse(argc, argv);
    if (!config) {
        NDS_LOG_ERROR("npu-peer", "options failed: {}", config.error().message);
        return EXIT_FAILURE;
    }
    return run(*config);
}

#include "rdma_benchmark_wire.hh"

#include "nds/logging.hh"
#include "ra/ra.hh"
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
    nds::benchmark::Operation operation{nds::benchmark::Operation::Read};
    std::uint32_t bytes{};
    std::uint32_t in_flight{kDefaultInFlight};
    std::uint32_t warmup{100U};
    std::uint32_t iterations{1000U};
    std::uint32_t wr_per_doorbell{1U};
    std::uint32_t max_wr_bytes{kDefaultMaxWrBytes};
    std::uint32_t max_wrs_per_window{kDefaultMaxWrsPerWindow};
    bool ring_last{};
};

nds::Result<Config> parse(int argc, char **argv) {
    Config config;
    std::string operation{"read"};
    CLI::App app{"Benchmark RA RDMA Read/Write from NPU memory to CPU DRAM."};
    app.add_option("--ascendcl", config.runtime.ascendcl_library)->required();
    app.add_option("--runtime", config.runtime.runtime_library)->required();
    app.add_option("--ra", config.transport.endpoint.ra_library)->required();
    app.add_option("--logical-device", config.runtime.logical_device_id)->required();
    app.add_option("--server", config.transport.server_address)->required();
    app.add_option("--operation", operation)->required()->check(CLI::IsMember({"read", "write"}));
    app.add_option("--bytes", config.bytes)->required()->check(CLI::Range(1U, UINT32_MAX));
    app.add_option("--in-flight", config.in_flight)->check(CLI::Range(1U, kMaxInFlight));
    app.add_option("--warmup", config.warmup)->check(CLI::Range(0U, UINT32_MAX));
    app.add_option("--iterations", config.iterations)->check(CLI::Range(1U, UINT32_MAX));
    app.add_option("--wr-per-doorbell", config.wr_per_doorbell)->check(CLI::Range(1U, kMaxInFlight));
    app.add_option("--max-wr-bytes", config.max_wr_bytes)->check(CLI::Range(0U, UINT32_MAX));
    app.add_option("--max-wrs-per-window", config.max_wrs_per_window)->check(CLI::Range(1U, UINT32_MAX));
    app.add_flag("--ring-last", config.ring_last,
                 "prepare all WRs in a window and ring only the final doorbell");
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

nds::Result<void> drain_completions(nds::client::QueuePair *qp, std::uint32_t expected) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kCompletionTimeoutMs);
    NdsDeviceWc completions[NDS_DEVICE_MAX_COMPLETIONS]{};
    std::uint32_t completed{};
    while (completed < expected && std::chrono::steady_clock::now() < deadline) {
        const std::uint32_t requested = std::min(expected - completed, NDS_DEVICE_MAX_COMPLETIONS);
        const auto polled = nds::NdsRaPollCq(qp, true, requested, completions);
        if (!polled)
            return nds::unexpected(polled.error());
        if (*polled == 0U)
            continue;
        for (std::uint32_t index = 0U; index < *polled; ++index) {
            if (completions[index].status != NDS_RA_WC_SUCCESS) {
                std::string detail = "RA send completion failed: status=" +
                                     std::to_string(completions[index].status) +
                                     ", vendor_error=" + std::to_string(completions[index].vendor_error) +
                                     ", opcode=" + std::to_string(completions[index].opcode) +
                                     ", qp_number=" + std::to_string(completions[index].qp_number);
                const auto qp_status = qp->query_status();
                if (qp_status)
                    detail += ", qp_status=" + std::to_string(*qp_status);
                else
                    detail += ", qp_status_query=" + qp_status.error().message;
                return nds::unexpected(nds::ErrorCode::kRa,
                                       detail);
            }
        }
        completed += *polled;
    }
    if (completed == expected)
        return {};
    return nds::unexpected(nds::ErrorCode::kRa,
                           "timed out waiting for RA send completions: " + std::to_string(completed) + "/" +
                               std::to_string(expected));
}

nds::Result<void> transfer_window(nds::client::Runtime *runtime, nds::client::Transport *transport,
                                  const nds::client::MemoryRegion &local, const nds::benchmark::RemoteMemory &remote,
                                  std::uint32_t bytes, std::uint32_t request_count, std::uint64_t *next_wr_id,
                                  std::uint32_t wr_per_doorbell, std::uint32_t max_wr_bytes, bool ring_last) {
    if (next_wr_id == nullptr || request_count == 0U)
        return nds::unexpected(nds::ErrorCode::kInvalidArgument, "benchmark window requires requests and a WR ID");
    const std::uint32_t chunk_bytes = max_wr_bytes == 0U ? bytes : std::min(bytes, max_wr_bytes);
    const std::uint64_t chunks_per_request = (static_cast<std::uint64_t>(bytes) + chunk_bytes - 1U) / chunk_bytes;
    const std::uint64_t total_wrs = static_cast<std::uint64_t>(request_count) * chunks_per_request;
    if (total_wrs > std::numeric_limits<std::size_t>::max() || total_wrs > UINT32_MAX)
        return nds::unexpected(nds::ErrorCode::kInvalidArgument, "benchmark WQE count exceeds supported range");
    std::vector<NdsRaSge> sges(static_cast<std::size_t>(total_wrs));
    NdsRaSendResponse last_response{};
    const std::uint32_t effective_wr_per_doorbell =
        ring_last ? static_cast<std::uint32_t>(total_wrs) : wr_per_doorbell;
    std::uint32_t expected_completions{};
    std::uint64_t wr_index{};
    for (std::uint32_t request = 0U; request < request_count; ++request) {
        const std::uint64_t base_offset = static_cast<std::uint64_t>(request) * bytes;
        for (std::uint64_t chunk_offset = 0U; chunk_offset < bytes; chunk_offset += chunk_bytes) {
            const std::uint32_t length =
                std::min(chunk_bytes, static_cast<std::uint32_t>(bytes - chunk_offset));
            const bool group_last = effective_wr_per_doorbell == 1U
                                        ? wr_index + 1U == total_wrs
                                        : (wr_index + 1U == total_wrs ||
                                           (wr_index + 1U) % effective_wr_per_doorbell == 0U);
            // RC ordering makes each signaled group boundary safe to reclaim.
            const NdsDeviceSendWr wr{(*next_wr_id)++,
                                     remote.operation == nds::benchmark::Operation::Read ? NDS_DEVICE_WR_RDMA_READ
                                                                                           : NDS_DEVICE_WR_RDMA_WRITE,
                                     group_last ? static_cast<std::uint32_t>(NDS_DEVICE_SEND_SIGNALED) : 0U,
                                     {local.address() + base_offset + chunk_offset, length, local.local_key()},
                                     remote.address + base_offset + chunk_offset,
                                     remote.remote_key,
                                     0U};
            if (effective_wr_per_doorbell > 1U) {
                const auto posted = nds::NdsRaPrepareSend(transport->qp(), wr, &sges[wr_index]);
                if (!posted)
                    return nds::unexpected(posted.error());
                last_response = *posted;
                if (group_last) {
                    if (const auto rung = nds::NdsRaRingSend(runtime, last_response); !rung)
                        return nds::unexpected(rung.error());
                    ++expected_completions;
                }
            } else if (const auto posted = nds::NdsRaPostSend(runtime, transport->qp(), wr); !posted) {
                return nds::unexpected(posted.error());
            } else if (group_last) {
                ++expected_completions;
            }
            ++wr_index;
        }
    }
    return drain_completions(transport->qp(), expected_completions);
}

nds::Result<void> transfer_operations(nds::client::Runtime *runtime, nds::client::Transport *transport,
                                      const nds::client::MemoryRegion &local,
                                      const nds::benchmark::RemoteMemory &remote, std::uint32_t bytes,
                                      std::uint32_t in_flight, std::uint32_t operations, std::uint64_t *next_wr_id,
                                      std::uint32_t wr_per_doorbell, std::uint32_t max_wr_bytes,
                                      std::uint32_t max_wrs_per_window, bool ring_last) {
    const std::uint32_t chunk_bytes = max_wr_bytes == 0U ? bytes : std::min(bytes, max_wr_bytes);
    const std::uint64_t wrs_per_request = (static_cast<std::uint64_t>(bytes) + chunk_bytes - 1U) / chunk_bytes;
    if (wrs_per_request > max_wrs_per_window)
        return nds::unexpected(nds::ErrorCode::kInvalidArgument,
                               "max WQEs per window is smaller than one logical transfer");
    const std::uint32_t requests_per_window = static_cast<std::uint32_t>(
        std::min<std::uint64_t>(in_flight, max_wrs_per_window / wrs_per_request));
    for (std::uint32_t completed = 0U; completed < operations;) {
        const std::uint32_t window = std::min(requests_per_window, operations - completed);
        if (const auto transferred = transfer_window(runtime, transport, local, remote, bytes, window, next_wr_id,
                                                     wr_per_doorbell, max_wr_bytes, ring_last);
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
    NDS_LOG_INFO("npu-client", "opening RA verbs transport");
    if (const auto opened = transport.open(&runtime, config->transport, {}); !opened) {
        NDS_LOG_ERROR("npu-client", "transport open failed: {}", opened.error().message);
        return EXIT_FAILURE;
    }
    NDS_LOG_INFO("npu-client", "RA verbs transport is connected");
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
    std::uint64_t wr_id = 1U;
    NDS_LOG_INFO("npu-client", "starting {} warmup operations", config->warmup);
    if (const auto completed = transfer_operations(&runtime, &transport, *local, remote, config->bytes,
                                                   config->in_flight, config->warmup, &wr_id,
                                                   config->wr_per_doorbell, config->max_wr_bytes,
                                                   config->max_wrs_per_window, config->ring_last);
        !completed) {
        NDS_LOG_ERROR("npu-client", "warmup failed: {}", completed.error().message);
        return EXIT_FAILURE;
    }
    NDS_LOG_INFO("npu-client", "starting {} measured operations", config->iterations);
    const auto start = std::chrono::steady_clock::now();
    if (const auto completed = transfer_operations(&runtime, &transport, *local, remote, config->bytes,
                                                   config->in_flight, config->iterations, &wr_id,
                                                   config->wr_per_doorbell, config->max_wr_bytes,
                                                   config->max_wrs_per_window, config->ring_last);
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
    const std::uint64_t wrs_per_request =
        (static_cast<std::uint64_t>(config->bytes) + output_chunk_bytes - 1U) / output_chunk_bytes;
    const std::uint64_t output_wr_per_doorbell =
        config->ring_last
            ? std::min<std::uint64_t>(config->in_flight, config->max_wrs_per_window / wrs_per_request) *
                  wrs_per_request
            : config->wr_per_doorbell;
    const std::uint8_t finished = 1U;
    if (const auto sent = transport.bootstrap()->send_bytes(&finished, sizeof(finished)); !sent) {
        NDS_LOG_ERROR("npu-client", "benchmark completion handshake failed: {}", sent.error().message);
        return EXIT_FAILURE;
    }
    std::cout << std::fixed << std::setprecision(3) << "{\"backend\":\"ra\",\"operation\":\""
              << nds::benchmark::operation_name(config->operation) << "\",\"bytes\":" << config->bytes
              << ",\"in_flight\":" << config->in_flight << ",\"warmup\":" << config->warmup
              << ",\"iterations\":" << config->iterations << ",\"wr_per_doorbell\":" << output_wr_per_doorbell
              << ",\"max_wr_bytes\":" << output_chunk_bytes
              << ",\"max_wrs_per_window\":" << config->max_wrs_per_window
              << ",\"completion_policy\":\"group-final-signaled\""
              << ",\"elapsed_ns\":"
              << std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count() << ",\"ops_per_second\":"
              << ops_per_second << ",\"gib_per_second\":" << gib_per_second << "}" << std::endl;
    return EXIT_SUCCESS;
}

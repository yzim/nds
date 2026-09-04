#include "backends/backend_mode.hh"
#include "backends/launcher.hh"
#include "logging.hh"
#include "result.hh"
#include "runtime.hh"
#include "storage.hh"
#include "transport.hh"

#include <CLI/CLI.hpp>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr std::uint32_t kTransferBytes = 256U * 1024U;
constexpr std::uint32_t kBatchEntries = 8U;
constexpr std::uint32_t kDefaultQpCount = 16U;
constexpr std::uint32_t kDefaultWarmupWindows = 2U;
constexpr std::uint32_t kDefaultMeasuredWindows = 100U;
constexpr std::int32_t kLaunchTimeoutMs = 30000;

struct Config {
    nds::client::RuntimeConfig runtime;
    nds::client::TransportConfig transport;
    nds::client::BackendConfig backend;
    std::string operation{"write"};
    std::uint32_t warmup_windows{kDefaultWarmupWindows};
    std::uint32_t measured_windows{kDefaultMeasuredWindows};
    std::string log_sink{"stderr"};
    std::string log_level{"info"};
};

struct QueueResources {
    nds::client::MemoryBuffer buffer;
    nds::client::MemoryRegion region;
    nds::client::MemoryBuffer entries;
    nds::client::MemoryRegion entries_region;
};

struct SlotWork {
    std::uint32_t slot_id{};
    std::uint64_t entries_address{};
    std::uint32_t entries_key{};
    std::uint32_t entry_count{};
};

nds::Result<Config> parse_args(int argc, char **argv, bool *exit_requested) {
    if (exit_requested == nullptr)
        return nds::Error{nds::ErrorCode::kInvalidArgument, "benchmark exit state is required"};
    Config config;
    config.transport.qp_count = kDefaultQpCount;
    std::string backend_mode{"ra"};
    CLI::App app{"Benchmark NDS storage throughput across multiple transport QPs."};
    app.add_option("--backend-mode", backend_mode, "Backend: ra, aiv, or aicpu")
        ->check(CLI::IsMember({"ra", "aiv", "aicpu"}));
    app.add_option("--backend-artifact-path", config.backend.artifact_path,
                   "RA backend shared artifact, AIV kernel object, or AICPU package descriptor");
    app.add_option("--logical-device", config.runtime.logical_device_id)->required();
    app.add_option("--server", config.transport.server_address)->required();
    app.add_option("--qp-count", config.transport.qp_count, "Transport QPs used by the benchmark")
        ->check(CLI::Range(1U, nds::wire::kMaxQpInfoBatch));
    app.add_option("--operation", config.operation, "Storage operation: read or write")
        ->check(CLI::IsMember({"read", "write"}));
    app.add_option("--warmup", config.warmup_windows)->check(CLI::Range(0U, UINT32_MAX));
    app.add_option("--iterations", config.measured_windows)->check(CLI::Range(1U, UINT32_MAX));
    app.add_option("--log-sink", config.log_sink)->check(CLI::IsMember({"stderr", "stdout", "syslog", "none"}));
    app.add_option("--log-level", config.log_level)
        ->check(CLI::IsMember({"trace", "debug", "info", "warn", "error", "critical", "off"}));
    try {
        app.parse(argc, argv);
    } catch (const CLI::CallForHelp &help) {
        *exit_requested = true;
        app.exit(help);
        return config;
    } catch (const CLI::ParseError &error) {
        return nds::Error{nds::ErrorCode::kInvalidArgument,
                          app.exit(error) == 0 ? "help requested" : "invalid options"};
    }

    if (backend_mode == "aicpu")
        config.backend.mode = nds::client::BackendMode::Aicpu;
    else if (backend_mode == "aiv")
        config.backend.mode = nds::client::BackendMode::Aiv;
    else
        config.backend.mode = nds::client::BackendMode::Ra;
    if (config.backend.artifact_path.empty())
        return nds::Error{nds::ErrorCode::kInvalidArgument, "--backend-artifact-path is required"};
    return config;
}

nds::Result<void> submit(nds::client::Launcher *launcher, const NdsStorageDescriptor &storage, const SlotWork &work,
                         bool read, void *stream) {
    if (launcher == nullptr)
        return nds::Error{nds::ErrorCode::kInvalidArgument, "storage benchmark launcher is empty"};
    if (read) {
        return launcher->with_config({.stream = stream, .sync = false, .sync_timeout_ms = kLaunchTimeoutMs})
            .storage_read_batch(storage, work.slot_id, work.entries_address, work.entries_key, work.entry_count);
    }
    return launcher->with_config({.stream = stream, .sync = false, .sync_timeout_ms = kLaunchTimeoutMs})
        .storage_write_batch(storage, work.slot_id, work.entries_address, work.entries_key, work.entry_count);
}

nds::Result<void> run_window(nds::client::StorageClient *client, nds::client::Launcher *launcher,
                             const std::vector<QueueResources> &queues, std::size_t slots_per_qp, bool read,
                             void *stream) {
    if (client == nullptr || launcher == nullptr || queues.empty() || slots_per_qp == 0U)
        return nds::Error{nds::ErrorCode::kInvalidArgument, "storage benchmark resources are incomplete"};

    std::vector<SlotWork> work;
    work.reserve(queues.size() * slots_per_qp);
    for (std::size_t allocation_index = 0U; allocation_index < queues.size() * slots_per_qp; ++allocation_index) {
        // Allocate in global slot-table order. The CPU posts each QP's receive
        // window in that order, while the client's queue-specific allocator
        // intentionally advances one global round-robin cursor.
        NDS_ASSIGN_OR_RETURN(const std::uint32_t slot_id, client->allocate_slot());
        const std::size_t queue_index = nds_storage_slot_id_queue(slot_id);
        const std::size_t slot_index = nds_storage_slot_id_index(slot_id);
        if (queue_index >= queues.size() || slot_index % queues.size() != queue_index ||
            slot_index / queues.size() >= slots_per_qp)
            return nds::Error{nds::ErrorCode::kProtocol, "storage benchmark received an unexpected slot mapping"};
        const std::size_t buffer_slot = slot_index / queues.size();
        const std::uint64_t entries_offset =
            buffer_slot * static_cast<std::size_t>(kBatchEntries) * nds::kStorageBatchEntryBytes;
        work.push_back({slot_id, queues[queue_index].entries_region.address() + entries_offset,
                        queues[queue_index].entries_region.remote_key(), kBatchEntries});
    }

    for (const SlotWork &request : work) {
        const auto submitted = submit(launcher, client->descriptor(), request, read, stream);
        if (!submitted.ok())
            return submitted.error();
    }
    for (const SlotWork &request : work) {
        NDS_RETURN_IF_ERROR(launcher->with_config({.stream = stream, .sync = true, .sync_timeout_ms = kLaunchTimeoutMs})
                                .storage_wait(client->descriptor(), request.slot_id));
        NDS_RETURN_IF_ERROR(client->release_slot(request.slot_id));
    }
    return {};
}

nds::Result<int> run(int argc, char **argv) {
    (void)nds::log::configure("storage-benchmark", "stderr", "info");
    bool exit_requested = false;
    NDS_ASSIGN_OR_RETURN(Config config, parse_args(argc, argv, &exit_requested));
    if (exit_requested)
        return EXIT_SUCCESS;
    NDS_RETURN_IF_ERROR(nds::log::configure("storage-benchmark", config.log_sink, config.log_level));

    nds::client::Runtime runtime;
    nds::client::Transport transport;
    nds::client::StorageClient client;
    NDS_RETURN_IF_ERROR(runtime.open(config.runtime));
    NDS_RETURN_IF_ERROR(transport.open(&runtime, config.transport, config.backend));
    NDS_RETURN_IF_ERROR(client.open(&runtime, &transport));
    NDS_ASSIGN_OR_RETURN(std::unique_ptr<nds::client::Launcher> launcher,
                         nds::client::Launcher::open(&runtime, config.backend.mode, config.backend.artifact_path));

    aclrtStream stream = nullptr;
    if (config.backend.mode != nds::client::BackendMode::Ra) {
        const int result = aclrtCreateStream(&stream);
        if (result != ACL_SUCCESS || stream == nullptr)
            return nds::Error{nds::ErrorCode::kRuntime,
                              "storage benchmark stream creation failed: " + std::to_string(result)};
    }
    struct StreamOwner {
        aclrtStream stream{};
        ~StreamOwner() {
            if (stream != nullptr)
                (void)aclrtDestroyStream(stream);
        }
    } stream_owner{stream};

    NDS_RETURN_IF_ERROR(launcher->with_config({.stream = stream, .sync = true, .sync_timeout_ms = kLaunchTimeoutMs})
                            .storage_bootstrap(client.bootstrap_descriptor()));
    NDS_RETURN_IF_ERROR(client.complete_bootstrap(static_cast<std::uint32_t>(kLaunchTimeoutMs)));

    const std::size_t qp_count = transport.qp_count();
    if (qp_count == 0U || client.slot_count() % qp_count != 0U)
        return nds::Error{nds::ErrorCode::kTransport, "storage benchmark received an invalid QP slot layout"};
    const std::size_t slots_per_qp = client.slot_count() / qp_count;
    if (slots_per_qp == 0U || slots_per_qp > std::numeric_limits<std::size_t>::max() / kBatchEntries ||
        slots_per_qp * kBatchEntries > std::numeric_limits<std::size_t>::max() / kTransferBytes ||
        slots_per_qp * kBatchEntries > std::numeric_limits<std::size_t>::max() / nds::kStorageBatchEntryBytes)
        return nds::Error{nds::ErrorCode::kInvalidArgument, "storage benchmark buffer size overflows"};
    const std::size_t transfers_per_qp = slots_per_qp * kBatchEntries;
    const std::size_t buffer_bytes = transfers_per_qp * kTransferBytes;
    const std::size_t entries_bytes = transfers_per_qp * nds::kStorageBatchEntryBytes;
    const std::uint64_t required_namespace = static_cast<std::uint64_t>(qp_count) * transfers_per_qp * kTransferBytes;
    if (required_namespace > client.capacity())
        return nds::Error{nds::ErrorCode::kProtocol,
                          "storage benchmark namespace is smaller than its multi-QP working set"};

    const bool read = config.operation == "read";
    std::vector<QueueResources> queues(qp_count);
    std::vector<std::uint8_t> pattern(buffer_bytes, 0x5aU);
    for (std::size_t queue_index = 0U; queue_index < queues.size(); ++queue_index) {
        QueueResources &queue = queues[queue_index];
        NDS_ASSIGN_OR_RETURN(queue.buffer, runtime.allocate(buffer_bytes, nds::client::MemoryLocation::Device));
        NDS_ASSIGN_OR_RETURN(queue.region,
                             client.register_memory(queue.buffer, nds::client::MemoryAccess::LocalWrite |
                                                                      nds::client::MemoryAccess::RemoteWrite |
                                                                      nds::client::MemoryAccess::RemoteRead));
        if (!read)
            NDS_RETURN_IF_ERROR(runtime.copy_to(&queue.buffer, pattern.data(), pattern.size()));

        NDS_ASSIGN_OR_RETURN(queue.entries, runtime.allocate(entries_bytes, nds::client::MemoryLocation::Device));
        NDS_ASSIGN_OR_RETURN(queue.entries_region,
                             client.register_memory(queue.entries, nds::client::MemoryAccess::LocalWrite |
                                                                       nds::client::MemoryAccess::RemoteRead));
        std::vector<std::uint8_t> encoded_entries(entries_bytes);
        for (std::size_t slot_index = 0U; slot_index < slots_per_qp; ++slot_index) {
            for (std::size_t entry_index = 0U; entry_index < kBatchEntries; ++entry_index) {
                const std::size_t transfer_index = slot_index * kBatchEntries + entry_index;
                const std::uint64_t offset =
                    (static_cast<std::uint64_t>(queue_index) * transfers_per_qp + transfer_index) * kTransferBytes;
                const nds::StorageMemory memory{queue.region.address() + transfer_index * kTransferBytes,
                                                kTransferBytes, queue.region.remote_key()};
                const std::size_t entry_offset = transfer_index * nds::kStorageBatchEntryBytes;
                const auto serialized = read ? nds::serialize_storage_batch_read_entry(
                                                   {offset, kTransferBytes, memory},
                                                   encoded_entries.data() + entry_offset, nds::kStorageBatchEntryBytes)
                                             : nds::serialize_storage_batch_write_entry(
                                                   {offset, kTransferBytes, memory},
                                                   encoded_entries.data() + entry_offset, nds::kStorageBatchEntryBytes);
                if (serialized != nds::StorageSerdeResult::Ok)
                    return nds::Error{nds::ErrorCode::kProtocol, "failed to serialize storage batch entry"};
            }
        }
        NDS_RETURN_IF_ERROR(runtime.copy_to(&queue.entries, encoded_entries.data(), encoded_entries.size()));
    }

    for (std::uint32_t index = 0U; index < config.warmup_windows; ++index)
        NDS_RETURN_IF_ERROR(run_window(&client, launcher.get(), queues, slots_per_qp, read, stream));

    const auto start = std::chrono::steady_clock::now();
    for (std::uint32_t index = 0U; index < config.measured_windows; ++index)
        NDS_RETURN_IF_ERROR(run_window(&client, launcher.get(), queues, slots_per_qp, read, stream));
    const auto elapsed = std::chrono::steady_clock::now() - start;

    const std::uint64_t bytes = static_cast<std::uint64_t>(config.measured_windows) * required_namespace;
    const std::uint64_t commands = static_cast<std::uint64_t>(config.measured_windows) * client.slot_count();
    const double seconds = std::chrono::duration<double>(elapsed).count();
    const double gib_per_second = static_cast<double>(bytes) / seconds / (1024.0 * 1024.0 * 1024.0);
    const double commands_per_second = static_cast<double>(commands) / seconds;
    std::cout << std::fixed << std::setprecision(3) << "{\"backend\":\""
              << (config.backend.mode == nds::client::BackendMode::Ra    ? "ra"
                  : config.backend.mode == nds::client::BackendMode::Aiv ? "aiv"
                                                                         : "aicpu")
              << "\",\"operation\":\"" << config.operation << "\",\"qps\":" << qp_count
              << ",\"slots_per_qp\":" << slots_per_qp << ",\"transfer_bytes\":" << kTransferBytes
              << ",\"batch_entries\":" << kBatchEntries << ",\"warmup_windows\":" << config.warmup_windows
              << ",\"measured_windows\":" << config.measured_windows << ",\"commands\":" << commands
              << ",\"elapsed_ns\":" << std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count()
              << ",\"commands_per_second\":" << commands_per_second << ",\"gib_per_second\":" << gib_per_second << "}"
              << std::endl;
    return EXIT_SUCCESS;
}

}  // namespace

int main(int argc, char **argv) {
    const nds::Result<int> result = run(argc, argv);
    if (!result.ok()) {
        NDS_LOG_ERROR("storage-benchmark", "storage benchmark failed: {}", result.error().message);
        return EXIT_FAILURE;
    }
    return result.value();
}

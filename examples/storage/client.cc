#include "logging.hh"
#include "result.hh"
#include "runtime.hh"
#include "storage.hh"
#include "transport.hh"
#include "backends/launcher.hh"

#include <CLI/CLI.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <acl/acl_rt.h>

namespace {

constexpr std::uint64_t kMaxTransferBytes = 64U * 1024U;
constexpr std::uint32_t kStorageCompletionTimeoutMs = 5000U;
// Two full eight-slot-per-QP windows exercise saturation and slot reuse.
constexpr std::uint32_t kStorageStressCommandCount = 32U;

struct CommandResources {
    std::vector<nds::client::MemoryBuffer> buffers;
    std::vector<nds::client::MemoryRegion> regions;
    std::vector<std::pair<std::uint64_t, std::uint32_t>> requests;
    nds::client::MemoryBuffer entries;
    nds::client::MemoryRegion entries_region;
    std::uint32_t slot_id{};
};

struct StreamOwner {
    aclrtStream stream{};
    ~StreamOwner() {
        if (stream != nullptr)
            (void)aclrtDestroyStream(stream);
    }
};

struct ClientConfig {
    nds::client::RuntimeConfig runtime;
    nds::client::TransportConfig transport;
    nds::client::BackendConfig backend;
    std::string operation{"write"};
    std::uint64_t offset{};
    std::uint32_t bytes{4096U};
    std::uint32_t batch_count{2U};
    std::string log_sink{"stderr"};
    std::string log_level{"info"};
};

nds::Result<int> parse_args(int argc, char **argv, ClientConfig *config, bool *exit_requested) {
    if (config == nullptr || exit_requested == nullptr)
        return nds::Error{nds::ErrorCode::kInvalidArgument, "client configuration and exit state are required"};
    CLI::App app{"Run NDS storage commands from an NPU against a CPU memory namespace."};
    std::string backend{"ra"};
    app.add_option("--backend-mode", backend, "Storage backend mode")->check(CLI::IsMember({"ra", "aicpu", "aiv"}));
    app.add_option("--backend-artifact-path", config->backend.artifact_path,
                   "RA backend shared artifact, AIV kernel object, or AICPU package descriptor");
    app.add_option("--operation", config->operation, "Storage operation")
        ->check(CLI::IsMember({"read", "write", "batch-read", "batch-write"}));
    app.add_option("--offset", config->offset, "Namespace byte offset");
    app.add_option("--bytes", config->bytes, "Storage transfer length")
        ->check(CLI::Range(std::uint32_t{1}, static_cast<std::uint32_t>(kMaxTransferBytes)));
    app.add_option("--batch-count", config->batch_count, "Number of entries in a batch storage command")
        ->check(CLI::Range(std::uint32_t{1}, nds::kStorageMaxBatchEntries));
    app.add_option("--logical-device", config->runtime.logical_device_id, "NPU logical device")->required();
    app.add_option("--qp-count", config->transport.qp_count, "Connected QPs to create")
        ->check(CLI::Range(1U, nds::wire::kMaxQpInfoBatch));
    app.add_option("--server", config->transport.server_address, "Server TCP exchange address as IPv4:port")
        ->required();
    app.add_option("--log-sink", config->log_sink, "Log sink")
        ->check(CLI::IsMember({"stderr", "stdout", "syslog", "none"}));
    app.add_option("--log-level", config->log_level, "Log level")
        ->check(CLI::IsMember({"trace", "debug", "info", "warn", "error", "critical", "off"}));
    try {
        app.parse(argc, argv);
    } catch (const CLI::CallForHelp &help) {
        *exit_requested = true;
        return app.exit(help);
    } catch (const CLI::ParseError &parse_error) {
        return app.exit(parse_error);
    }

    if (backend == "aicpu")
        config->backend.mode = nds::client::BackendMode::Aicpu;
    if (backend == "aiv")
        config->backend.mode = nds::client::BackendMode::Aiv;
    if (config->backend.artifact_path.empty()) {
        return nds::Error{nds::ErrorCode::kInvalidArgument, "invalid option combination"};
    }
    return 0;
}

}  // namespace

nds::Result<int> run(int argc, char **argv) {
    (void)nds::log::configure("npu-client", "stderr", "info");
    ClientConfig config;
    config.transport.tcp_timeout_ms = 10000U;
    bool exit_requested = false;
    NDS_ASSIGN_OR_RETURN(int parse_status, parse_args(argc, argv, &config, &exit_requested));
    if (exit_requested || parse_status != 0)
        return parse_status;
    NDS_RETURN_IF_ERROR(nds::log::configure("npu-client", config.log_sink, config.log_level));

    nds::client::Runtime runtime;
    nds::client::Transport transport;
    nds::client::StorageClient client;
    NDS_RETURN_IF_ERROR(runtime.open(config.runtime));
    NDS_RETURN_IF_ERROR(transport.open(&runtime, config.transport, config.backend));
    NDS_RETURN_IF_ERROR(client.open(&runtime, &transport));
    NDS_ASSIGN_OR_RETURN(std::unique_ptr<nds::client::Launcher> launcher,
                         nds::client::Launcher::open(&runtime, config.backend.mode, config.backend.artifact_path));
    StreamOwner stream;
    if (config.backend.mode != nds::client::BackendMode::Ra) {
        const int result = aclrtCreateStream(&stream.stream);
        if (result != ACL_SUCCESS || stream.stream == nullptr)
            return nds::Error{nds::ErrorCode::kRuntime, "storage stream creation failed: " + std::to_string(result)};
    }
    NDS_RETURN_IF_ERROR(launcher->with_config({.stream = stream.stream, .sync = true, .sync_timeout_ms = 5000})
                            .storage_bootstrap(client.bootstrap_descriptor()));
    NDS_RETURN_IF_ERROR(client.complete_bootstrap(5000U));
    const bool batch = config.operation == "batch-read" || config.operation == "batch-write";
    const bool read = config.operation == "read" || config.operation == "batch-read";
    const std::uint32_t entries_per_command = batch ? config.batch_count : 1U;
    const std::uint64_t total_entry_count =
        static_cast<std::uint64_t>(kStorageStressCommandCount) * entries_per_command;
    if (total_entry_count == 0U ||
        (total_entry_count - 1U) > std::numeric_limits<std::uint64_t>::max() / config.bytes ||
        config.offset > std::numeric_limits<std::uint64_t>::max() - (total_entry_count - 1U) * config.bytes)
        return nds::Error{nds::ErrorCode::kInvalidArgument, "storage workload offsets overflow"};
    const std::size_t qp_count = transport.qp_count();
    if (qp_count == 0U)
        return nds::Error{nds::ErrorCode::kTransport, "storage transport has no negotiated QPs"};

    std::vector<CommandResources> commands(kStorageStressCommandCount);
    for (std::uint32_t command_index = 0U; command_index < kStorageStressCommandCount; ++command_index) {
        CommandResources &resources = commands[command_index];
        resources.buffers.reserve(entries_per_command);
        resources.requests.reserve(entries_per_command);
        for (std::uint32_t entry_index = 0U; entry_index < entries_per_command; ++entry_index) {
            const std::uint64_t entry_number =
                static_cast<std::uint64_t>(command_index) * entries_per_command + entry_index;
            const std::uint64_t operation_offset = config.offset + entry_number * config.bytes;
            NDS_ASSIGN_OR_RETURN(nds::client::MemoryBuffer buffer,
                                 runtime.allocate(config.bytes, nds::client::MemoryLocation::Device));
            resources.buffers.push_back(std::move(buffer));
            NDS_ASSIGN_OR_RETURN(
                nds::client::MemoryRegion region,
                client.register_memory(resources.buffers.back(), nds::client::MemoryAccess::LocalWrite |
                                                                     nds::client::MemoryAccess::RemoteWrite |
                                                                     nds::client::MemoryAccess::RemoteRead));
            resources.regions.push_back(std::move(region));
            resources.requests.emplace_back(operation_offset, config.bytes);
            if (!read) {
                std::vector<std::uint8_t> payload(config.bytes);
                for (std::size_t index = 0U; index < payload.size(); ++index)
                    payload[index] = static_cast<std::uint8_t>((operation_offset + index) ^ 0x5aU);
                NDS_RETURN_IF_ERROR(runtime.copy_to(&resources.buffers.back(), payload.data(), payload.size()));
            }
        }
    }

    const std::size_t submission_window = client.slot_count();
    for (std::uint32_t window_start = 0U; window_start < kStorageStressCommandCount;
         window_start += static_cast<std::uint32_t>(submission_window)) {
        const std::uint32_t window_end =
            std::min<std::uint32_t>(kStorageStressCommandCount, window_start + submission_window);
        for (std::uint32_t command_index = window_start; command_index < window_end; ++command_index) {
            CommandResources &resources = commands[command_index];
            NDS_ASSIGN_OR_RETURN(resources.slot_id, client.allocate_slot());
            nds::Result<void> submitted;
            if (batch) {
                std::vector<std::uint8_t> entries_bytes(resources.requests.size() * nds::kStorageBatchEntryBytes);
                for (std::size_t index = 0U; index < resources.requests.size(); ++index) {
                    const auto &request = resources.requests[index];
                    const nds::StorageMemory memory{resources.regions[index].address(),
                                                    resources.regions[index].length(),
                                                    resources.regions[index].remote_key()};
                    if (read) {
                        const nds::StorageBatchReadEntry entry{request.first, request.second, memory};
                        if (nds::serialize_storage_batch_read_entry(
                                entry, entries_bytes.data() + index * nds::kStorageBatchEntryBytes,
                                nds::kStorageBatchEntryBytes) != nds::StorageSerdeResult::Ok)
                            return nds::Error{nds::ErrorCode::kProtocol,
                                              "failed to serialize storage batch Read entry"};
                    } else {
                        const nds::StorageBatchWriteEntry entry{request.first, request.second, memory};
                        if (nds::serialize_storage_batch_write_entry(
                                entry, entries_bytes.data() + index * nds::kStorageBatchEntryBytes,
                                nds::kStorageBatchEntryBytes) != nds::StorageSerdeResult::Ok)
                            return nds::Error{nds::ErrorCode::kProtocol,
                                              "failed to serialize storage batch Write entry"};
                    }
                }
                NDS_ASSIGN_OR_RETURN(resources.entries,
                                     runtime.allocate(entries_bytes.size(), nds::client::MemoryLocation::Device));
                NDS_RETURN_IF_ERROR(runtime.copy_to(&resources.entries, entries_bytes.data(), entries_bytes.size()));
                NDS_ASSIGN_OR_RETURN(
                    nds::client::MemoryRegion entries_region,
                    client.register_memory(resources.entries, nds::client::MemoryAccess::LocalWrite |
                                                                  nds::client::MemoryAccess::RemoteRead));
                resources.entries_region = std::move(entries_region);
                submitted =
                    read ? launcher->with_config({.stream = stream.stream, .sync = false, .sync_timeout_ms = 5000})
                               .storage_read_batch(client.descriptor(), resources.slot_id,
                                                   resources.entries_region.address(),
                                                   resources.entries_region.remote_key(),
                                                   static_cast<std::uint32_t>(resources.requests.size()))
                         : launcher->with_config({.stream = stream.stream, .sync = false, .sync_timeout_ms = 5000})
                               .storage_write_batch(client.descriptor(), resources.slot_id,
                                                    resources.entries_region.address(),
                                                    resources.entries_region.remote_key(),
                                                    static_cast<std::uint32_t>(resources.requests.size()));
            } else {
                const auto &region = resources.regions.front();
                submitted =
                    read ? launcher->with_config({.stream = stream.stream, .sync = false, .sync_timeout_ms = 5000})
                               .storage_read(client.descriptor(), resources.slot_id, resources.requests.front().first,
                                             region.address(), region.remote_key(), config.bytes)
                         : launcher->with_config({.stream = stream.stream, .sync = false, .sync_timeout_ms = 5000})
                               .storage_write(client.descriptor(), resources.slot_id, resources.requests.front().first,
                                              region.address(), region.remote_key(), config.bytes);
            }
            NDS_RETURN_IF_ERROR(submitted);
        }
        for (std::uint32_t command_index = window_start; command_index < window_end; ++command_index) {
            CommandResources &resources = commands[command_index];
            NDS_RETURN_IF_ERROR(
                launcher
                    ->with_config({.stream = stream.stream,
                                   .sync = true,
                                   .sync_timeout_ms = static_cast<std::int32_t>(kStorageCompletionTimeoutMs)})
                    .storage_wait(client.descriptor(), resources.slot_id));
            NDS_RETURN_IF_ERROR(client.release_slot(resources.slot_id));
            if (read) {
                for (std::size_t index = 0U; index < resources.requests.size(); ++index) {
                    const auto &request = resources.requests[index];
                    std::vector<std::uint8_t> observed(request.second);
                    NDS_RETURN_IF_ERROR(runtime.copy_from(observed.data(), resources.buffers[index], observed.size()));
                    for (std::size_t index = 0U; index < observed.size(); ++index) {
                        if (observed[index] != static_cast<std::uint8_t>((request.first + index) ^ 0x5aU)) {
                            return nds::Error{nds::ErrorCode::kProtocol,
                                              "storage Read verification failed at namespace byte " +
                                                  std::to_string(request.first + index)};
                        }
                    }
                }
            }
        }
    }

    NDS_LOG_INFO("npu-client", "CPU completed {} NDS storage commands across {} QPs.", kStorageStressCommandCount,
                 qp_count);
    return EXIT_SUCCESS;
}

int main(int argc, char **argv) {
    const nds::Result<int> run_result = run(argc, argv);
    if (!run_result.ok()) {
        NDS_LOG_ERROR("npu-client", "storage example failed: {}", run_result.error().message);
        return EXIT_FAILURE;
    }
    return run_result.value();
}

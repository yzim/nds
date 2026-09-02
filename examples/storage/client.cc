#include "logging.hh"
#include "result.hh"
#include "runtime.hh"
#include "storage.hh"
#include "transport.hh"

#include <CLI/CLI.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

namespace {

constexpr std::uint64_t kMaxTransferBytes = 64U * 1024U;
constexpr std::uint32_t kStorageCompletionTimeoutMs = 5000U;
// Two full four-slot-per-QP windows exercise saturation and slot reuse.
constexpr std::uint32_t kStorageStressCommandCount = 32U;

struct CommandResources {
    std::vector<nds::client::MemoryBuffer> buffers;
    std::vector<nds::client::StorageIo> requests;
    nds::client::StorageCompletionHandle completion{};
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

int main(int argc, char **argv) {
    (void)nds::log::configure("npu-client", "stderr", "info");
    ClientConfig config;
    config.transport.tcp_timeout_ms = 10000U;
    bool exit_requested = false;
    const auto parse_result = parse_args(argc, argv, &config, &exit_requested);
    if (!parse_result.ok()) {
        NDS_LOG_ERROR("npu-client", "{}", parse_result.error().message);
        return EXIT_FAILURE;
    }
    if (exit_requested || parse_result.value() != 0) {
        return parse_result.value();
    }
    if (const auto configured = nds::log::configure("npu-client", config.log_sink, config.log_level);
        !configured.ok()) {
        NDS_LOG_ERROR("npu-client", "invalid logger configuration: {}", configured.error().message);
        return EXIT_FAILURE;
    }

    nds::client::Runtime runtime;
    nds::client::Transport transport;
    nds::client::StorageClient client;
    if (const auto result = runtime.open(config.runtime); !result.ok()) {
        NDS_LOG_ERROR("npu-client", "runtime open failed: {}", result.error().message);
        return EXIT_FAILURE;
    }
    if (const auto result = transport.open(&runtime, config.transport, config.backend); !result.ok()) {
        NDS_LOG_ERROR("npu-client", "transport open failed: {}", result.error().message);
        return EXIT_FAILURE;
    }
    if (const auto result = client.open(&runtime, &transport); !result.ok()) {
        NDS_LOG_ERROR("npu-client", "storage client open failed: {}", result.error().message);
        return EXIT_FAILURE;
    }
    const bool batch = config.operation == "batch-read" || config.operation == "batch-write";
    const bool read = config.operation == "read" || config.operation == "batch-read";
    const std::uint32_t entries_per_command = batch ? config.batch_count : 1U;
    const std::uint64_t total_entry_count = static_cast<std::uint64_t>(kStorageStressCommandCount) *
                                             entries_per_command;
    if (total_entry_count == 0U ||
        (total_entry_count - 1U) > std::numeric_limits<std::uint64_t>::max() / config.bytes ||
        config.offset > std::numeric_limits<std::uint64_t>::max() -
                             (total_entry_count - 1U) * config.bytes) {
        NDS_LOG_ERROR("npu-client", "storage workload offsets overflow");
        return EXIT_FAILURE;
    }
    const std::size_t qp_count = transport.qp_count();
    if (qp_count == 0U) {
        NDS_LOG_ERROR("npu-client", "storage transport has no negotiated QPs");
        return EXIT_FAILURE;
    }

    std::vector<CommandResources> commands(kStorageStressCommandCount);
    for (std::uint32_t command_index = 0U; command_index < kStorageStressCommandCount; ++command_index) {
        CommandResources &resources = commands[command_index];
        resources.buffers.reserve(entries_per_command);
        resources.requests.reserve(entries_per_command);
        for (std::uint32_t entry_index = 0U; entry_index < entries_per_command; ++entry_index) {
            const std::uint64_t entry_number = static_cast<std::uint64_t>(command_index) * entries_per_command +
                                               entry_index;
            const std::uint64_t operation_offset = config.offset + entry_number * config.bytes;
            auto allocated = runtime.allocate(config.bytes, nds::client::MemoryLocation::Device);
            if (!allocated.ok()) {
                NDS_LOG_ERROR("npu-client", "client application buffer allocation failed: {}",
                              allocated.error().message);
                return EXIT_FAILURE;
            }
            resources.buffers.push_back(std::move(allocated).value());
            resources.requests.push_back(nds::client::StorageIo{
                .offset = operation_offset,
                .data = &resources.buffers.back(),
                .length = config.bytes,
            });
            if (!read) {
                std::vector<std::uint8_t> payload(config.bytes);
                for (std::size_t index = 0U; index < payload.size(); ++index)
                    payload[index] = static_cast<std::uint8_t>((operation_offset + index) ^ 0x5aU);
                if (const auto copied = runtime.copy_to(&resources.buffers.back(), payload.data(), payload.size());
                    !copied.ok()) {
                    NDS_LOG_ERROR("npu-client", "client application buffer copy failed: {}", copied.error().message);
                    return EXIT_FAILURE;
                }
            }
        }
    }

    const std::size_t submission_window = client.slot_count();
    for (std::uint32_t window_start = 0U; window_start < kStorageStressCommandCount;
         window_start += static_cast<std::uint32_t>(submission_window)) {
        const std::uint32_t window_end = std::min<std::uint32_t>(kStorageStressCommandCount,
                                                                  window_start + submission_window);
        for (std::uint32_t command_index = window_start; command_index < window_end; ++command_index) {
            CommandResources &resources = commands[command_index];
            const auto submitted = batch
                                       ? (read ? client.read_batch(resources.requests) :
                                                client.write_batch(resources.requests))
                                       : (read ? client.read(resources.requests.front().offset,
                                                             resources.requests.front().data,
                                                             resources.requests.front().length)
                                               : client.write(resources.requests.front().offset,
                                                              resources.requests.front().data,
                                                              resources.requests.front().length));
            if (!submitted.ok()) {
                NDS_LOG_ERROR("npu-client", "storage {} command {} failed: {}", config.operation, command_index,
                              submitted.error().message);
                return EXIT_FAILURE;
            }
            resources.completion = submitted.value();
        }
        for (std::uint32_t command_index = window_start; command_index < window_end; ++command_index) {
            CommandResources &resources = commands[command_index];
            if (const auto completed = client.wait(resources.completion, kStorageCompletionTimeoutMs);
                !completed.ok()) {
                NDS_LOG_ERROR("npu-client", "storage {} command {} completion failed: {}", config.operation,
                              command_index, completed.error().message);
                return EXIT_FAILURE;
            }
            if (read) {
                for (const nds::client::StorageIo &request : resources.requests) {
                    std::vector<std::uint8_t> observed(request.length);
                    if (const auto copied = runtime.copy_from(observed.data(), *request.data, observed.size());
                        !copied.ok()) {
                        NDS_LOG_ERROR("npu-client", "client Read copy failed: {}", copied.error().message);
                        return EXIT_FAILURE;
                    }
                    for (std::size_t index = 0U; index < observed.size(); ++index) {
                        if (observed[index] != static_cast<std::uint8_t>((request.offset + index) ^ 0x5aU)) {
                            NDS_LOG_ERROR("npu-client", "storage Read verification failed at namespace byte {}",
                                          request.offset + index);
                            return EXIT_FAILURE;
                        }
                    }
                }
            }
        }
    }

    NDS_LOG_INFO("npu-client", "CPU completed {} NDS storage commands across {} QPs.",
                 kStorageStressCommandCount, qp_count);
    return EXIT_SUCCESS;
}

#include "logging.hh"
#include "result.hh"
#include "runtime.hh"
#include "storage.hh"
#include "transport.hh"

#include <CLI/CLI.hpp>

#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

namespace {

constexpr std::uint64_t kMaxTransferBytes = 64U * 1024U;
constexpr std::uint32_t kStorageCompletionTimeoutMs = 5000U;
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
        return nds::unexpected(nds::ErrorCode::kInvalidArgument, "client configuration and exit state are required");
    CLI::App app{"Send one NDS storage command from an NPU to a CPU memory namespace."};
    app.add_option("--cann-runtime", config->runtime.cann_runtime_library, "CANN runtime shared library")->required();
    app.add_option("--ra", config->transport.endpoint.ra_library, "CANN RA shared library")->required();
    std::string backend{"ra"};
    app.add_option("--backend", backend, "Storage backend mode")->check(CLI::IsMember({"ra", "aicpu", "aiv"}));
    app.add_option("--aicpu-kernel", config->backend.aicpu_kernel, "AICPU standard-kernel package");
    app.add_option("--aiv-kernel", config->backend.aiv_kernel, "AIV kernel binary");
    app.add_option("--operation", config->operation, "Storage operation")
        ->check(CLI::IsMember({"read", "write", "batch-read", "batch-write"}));
    app.add_option("--offset", config->offset, "Namespace byte offset");
    app.add_option("--bytes", config->bytes, "Storage transfer length")
        ->check(CLI::Range(std::uint32_t{1}, static_cast<std::uint32_t>(kMaxTransferBytes)));
    app.add_option("--batch-count", config->batch_count, "Number of entries in a batch storage command")
        ->check(CLI::Range(std::uint32_t{1}, nds::kStorageMaxBatchEntries));
    app.add_option("--logical-device", config->runtime.logical_device_id, "NPU logical device")->required();
    app.add_option("--port", config->transport.qp.port_num, "NPU RoCE port")
        ->check(CLI::Range(std::uint16_t{1}, std::numeric_limits<std::uint16_t>::max()));
    app.add_option("--path-mtu", config->transport.qp.path_mtu, "Path MTU")
        ->check(CLI::Range(std::uint16_t{1}, std::numeric_limits<std::uint16_t>::max()));
    app.add_option("--qp-count", config->transport.qp_count, "Connected QPs to create")
        ->check(CLI::Range(1U, nds::wire::kMaxQpInfoBatch));
    app.add_option("--server", config->transport.server_address, "Server TCP exchange address as IPv4:port")
        ->required();
    app.add_option("--tcp-timeout-ms", config->transport.tcp_timeout_ms, "TCP server bootstrap timeout")
        ->check(CLI::PositiveNumber);
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
        config->backend.mode = nds::client::NpuBackend::Aicpu;
    if (backend == "aiv")
        config->backend.mode = nds::client::NpuBackend::Aiv;
    if ((backend == "aicpu" && config->backend.aicpu_kernel.empty()) ||
        (backend == "aiv" && config->backend.aiv_kernel.empty())) {
        return nds::unexpected(nds::ErrorCode::kInvalidArgument, "invalid option combination");
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
    if (!parse_result) {
        NDS_LOG_ERROR("npu-client", "{}", parse_result.error().message);
        return EXIT_FAILURE;
    }
    if (exit_requested || *parse_result != 0) {
        return *parse_result;
    }
    if (const auto configured = nds::log::configure("npu-client", config.log_sink, config.log_level); !configured) {
        NDS_LOG_ERROR("npu-client", "invalid logger configuration: {}", configured.error().message);
        return EXIT_FAILURE;
    }

    nds::client::Runtime runtime;
    nds::client::Transport transport;
    nds::client::StorageClient client;
    if (const auto result = runtime.open(config.runtime); !result) {
        NDS_LOG_ERROR("npu-client", "runtime open failed: {}", result.error().message);
        return EXIT_FAILURE;
    }
    if (const auto result = transport.open(&runtime, config.transport, config.backend); !result) {
        NDS_LOG_ERROR("npu-client", "transport open failed: {}", result.error().message);
        return EXIT_FAILURE;
    }
    if (const auto result = client.open(&runtime, &transport); !result) {
        NDS_LOG_ERROR("npu-client", "storage client open failed: {}", result.error().message);
        return EXIT_FAILURE;
    }
    const bool batch = config.operation == "batch-read" || config.operation == "batch-write";
    const bool read = config.operation == "read" || config.operation == "batch-read";
    const std::uint32_t operation_count = batch ? config.batch_count : 1U;
    const std::uint64_t final_offset_delta = static_cast<std::uint64_t>(operation_count - 1U) * config.bytes;
    if (final_offset_delta > std::numeric_limits<std::uint64_t>::max() - config.offset) {
        NDS_LOG_ERROR("npu-client", "batch storage offsets overflow");
        return EXIT_FAILURE;
    }
    std::vector<nds::client::MemoryBuffer> buffers;
    std::vector<nds::client::StorageIo> requests;
    buffers.reserve(operation_count);
    requests.reserve(operation_count);
    for (std::uint32_t operation_index = 0U; operation_index < operation_count; ++operation_index) {
        const std::uint64_t operation_offset = config.offset + operation_index * config.bytes;
        auto allocated = runtime.allocate(config.bytes);
        if (!allocated) {
            NDS_LOG_ERROR("npu-client", "client application buffer allocation failed: {}", allocated.error().message);
            return EXIT_FAILURE;
        }
        buffers.push_back(std::move(*allocated));
        requests.push_back({operation_offset, &buffers.back(), config.bytes});
        if (!read) {
            std::vector<std::uint8_t> payload(config.bytes);
            for (std::size_t index = 0U; index < payload.size(); ++index)
                payload[index] = static_cast<std::uint8_t>((operation_offset + index) ^ 0x5aU);
            if (const auto copied = runtime.copy_to(&buffers.back(), payload.data(), payload.size()); !copied) {
                NDS_LOG_ERROR("npu-client", "client application buffer copy failed: {}", copied.error().message);
                return EXIT_FAILURE;
            }
        }
    }

    nds::Result<nds::client::StorageCompletionHandle> result;
    if (batch)
        result = read ? client.read_batch(requests) : client.write_batch(requests);
    else
        result = read ? client.read(config.offset, &buffers.front(), config.bytes)
                      : client.write(config.offset, &buffers.front(), config.bytes);
    if (!result) {
        NDS_LOG_ERROR("npu-client", "storage {} failed: {}", config.operation, result.error().message);
        return EXIT_FAILURE;
    }
    if (const auto completed = client.wait(*result, kStorageCompletionTimeoutMs); !completed) {
        NDS_LOG_ERROR("npu-client", "storage {} completion failed: {}", config.operation, completed.error().message);
        return EXIT_FAILURE;
    }
    if (read) {
        for (const nds::client::StorageIo &request : requests) {
            std::vector<std::uint8_t> observed(request.length);
            if (const auto copied = runtime.copy_from(observed.data(), *request.data, observed.size()); !copied) {
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
    NDS_LOG_INFO("npu-client", "CPU completed the NDS storage command.");
    return EXIT_SUCCESS;
}

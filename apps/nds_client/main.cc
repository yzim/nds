#include "nds/logging.hh"
#include "nds/result.hh"
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
struct ClientConfig {
    nds::client::RuntimeConfig runtime;
    nds::client::TransportConfig transport;
    nds::client::ExecutionConfig execution;
    std::string operation{"write"};
    std::uint64_t offset{};
    std::uint32_t bytes{4096U};
    std::string log_sink{"stderr"};
    std::string log_level{"info"};
};

nds::Result<int> parse_args(int argc, char **argv, ClientConfig *config, bool *exit_requested) {
    if (config == nullptr || exit_requested == nullptr)
        return nds::unexpected(nds::ErrorCode::kInvalidArgument, "client configuration and exit state are required");
    CLI::App app{"Send one NDS storage command from an NPU to a CPU memory namespace."};
    app.add_option("--ascendcl", config->runtime.ascendcl_library, "AscendCL shared library")->required();
    app.add_option("--runtime", config->runtime.runtime_library, "CANN runtime shared library")->required();
    app.add_option("--ra", config->transport.endpoint.ra_library, "CANN RA shared library")->required();
    std::string execution{"ra"};
    app.add_option("--execution", execution, "Storage execution mode")->check(CLI::IsMember({"ra", "aicpu", "aiv"}));
    app.add_option("--aicpu-kernel-config", config->execution.aicpu_kernel_config,
                   "AICPU standard-kernel package configuration");
    app.add_option("--aiv-kernel", config->execution.aiv_kernel, "AIV kernel binary");
    app.add_option("--operation", config->operation, "Storage operation")->check(CLI::IsMember({"read", "write"}));
    app.add_option("--offset", config->offset, "Namespace byte offset");
    app.add_option("--bytes", config->bytes, "Storage transfer length")
        ->check(CLI::Range(std::uint32_t{1}, static_cast<std::uint32_t>(kMaxTransferBytes)));
    app.add_option("--npu-ip", config->transport.endpoint.local_ipv4, "NPU RoCE IPv4 address")->required();
    app.add_option("--logical-device", config->runtime.logical_device_id, "NPU logical device")->required();
    app.add_option("--physical-device", config->transport.endpoint.physical_device_id, "NPU physical device")
        ->required();
    app.add_option("--port", config->transport.qp.port_num, "NPU RoCE port")
        ->check(CLI::Range(std::uint16_t{1}, std::numeric_limits<std::uint16_t>::max()));
    app.add_option("--path-mtu", config->transport.qp.path_mtu, "Path MTU")
        ->check(CLI::Range(std::uint16_t{1}, std::numeric_limits<std::uint16_t>::max()));
    app.add_option("--cpu-ip", config->transport.cpu_ipv4, "CPU peer IPv4 address")->required();
    app.add_option("--tcp-port", config->transport.tcp_port, "TCP peer-exchange port")
        ->check(CLI::Range(std::uint16_t{1}, std::numeric_limits<std::uint16_t>::max()));
    app.add_option("--tcp-timeout-ms", config->transport.tcp_timeout_ms, "TCP peer-exchange timeout")
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

    if (execution == "aicpu")
        config->execution.mode = nds::client::NpuExecutionMode::Aicpu;
    if (execution == "aiv")
        config->execution.mode = nds::client::NpuExecutionMode::Aiv;
    if ((execution == "aicpu" && config->execution.aicpu_kernel_config.empty()) ||
        (execution == "aiv" && config->execution.aiv_kernel.empty())) {
        return nds::unexpected(nds::ErrorCode::kInvalidArgument, "invalid option combination");
    }
    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    (void)nds::log::configure("npu-client", "stderr", "info");
    ClientConfig config;
    config.transport.tcp_port = 18515U;
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
    if (const auto result = transport.open(&runtime, config.transport, config.execution); !result) {
        NDS_LOG_ERROR("npu-client", "transport open failed: {}", result.error().message);
        return EXIT_FAILURE;
    }
    if (const auto result = client.open(&runtime, &transport); !result) {
        NDS_LOG_ERROR("npu-client", "storage client open failed: {}", result.error().message);
        return EXIT_FAILURE;
    }
    std::vector<std::uint8_t> payload(config.bytes);
    for (std::size_t index = 0; index < payload.size(); ++index) {
        payload[index] = static_cast<std::uint8_t>(index ^ 0x5aU);
    }
    auto allocated = runtime.allocate(payload.size());
    if (!allocated) {
        NDS_LOG_ERROR("npu-client", "client application buffer allocation failed: {}", allocated.error().message);
        return EXIT_FAILURE;
    }
    nds::client::MemoryBuffer data = std::move(*allocated);
    if (config.operation == "write") {
        if (const auto copied = runtime.copy_to(&data, payload.data(), payload.size()); !copied) {
            NDS_LOG_ERROR("npu-client", "client application buffer copy failed: {}", copied.error().message);
            return EXIT_FAILURE;
        }
    }

    const auto result = config.operation == "read" ? client.read(config.offset, &data, config.bytes)
                                                   : client.write(config.offset, &data, config.bytes);
    if (!result) {
        NDS_LOG_ERROR("npu-client", "storage {} failed: {}", config.operation, result.error().message);
        return EXIT_FAILURE;
    }
    if (config.operation == "read") {
        std::vector<std::uint8_t> result(payload.size());
        if (const auto copied = runtime.copy_from(result.data(), data, result.size()); !copied) {
            NDS_LOG_ERROR("npu-client", "client Read copy failed: {}", copied.error().message);
            return EXIT_FAILURE;
        }
        if (result != payload) {
            NDS_LOG_ERROR("npu-client", "storage Read verification failed");
            return EXIT_FAILURE;
        }
    }
    NDS_LOG_INFO("npu-client", "CPU completed the NDS storage command.");
    return EXIT_SUCCESS;
}

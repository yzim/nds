#include "nds/logging.hh"
#include "nds/result.hh"
#include "storage.hh"

#include <CLI/CLI.hpp>

#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

namespace {

constexpr std::uint64_t kMaxTransferBytes = 64U * 1024U;
struct ClientConfig {
    nds::client::TransportConfig transport;
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
    app.add_option("--ascendcl", config->transport.context.ascendcl_library, "AscendCL shared library")->required();
    app.add_option("--runtime", config->transport.context.runtime_library, "CANN runtime shared library")->required();
    app.add_option("--ra", config->transport.context.ra_library, "CANN RA shared library")->required();
    std::string execution{"host-ra"};
    app.add_option("--execution", execution, "Storage execution mode")
        ->check(CLI::IsMember({"host-ra", "aicpu", "aiv"}));
    app.add_option("--aicpu-kernel-config", config->transport.rma.aicpu_kernel_config,
                   "AICPU kernel package configuration");
    app.add_option("--aiv-kernel", config->transport.rma.aiv_kernel, "AIV kernel binary");
    app.add_option("--operation", config->operation, "Storage operation")->check(CLI::IsMember({"read", "write"}));
    app.add_option("--offset", config->offset, "Namespace byte offset");
    app.add_option("--bytes", config->bytes, "Storage transfer length")
        ->check(CLI::Range(std::uint32_t{1}, static_cast<std::uint32_t>(kMaxTransferBytes)));
    app.add_option("--npu-ip", config->transport.qp.local_ipv4, "NPU RoCE IPv4 address")->required();
    app.add_option("--logical-device", config->transport.context.logical_device_id, "NPU logical device")->required();
    app.add_option("--physical-device", config->transport.context.physical_device_id, "NPU physical device")
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
        config->transport.execution = nds::NpuExecutionMode::Aicpu;
    if (execution == "aiv")
        config->transport.execution = nds::NpuExecutionMode::Aiv;
    config->transport.qp.physical_device_id = config->transport.context.physical_device_id;
    if ((execution == "aicpu" && config->transport.rma.aicpu_kernel_config.empty()) ||
        (execution == "aiv" && config->transport.rma.aiv_kernel.empty())) {
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

    nds::client::StorageClient client;
    if (const auto result = client.open(config.transport); !result) {
        NDS_LOG_ERROR("npu-client", "storage client open failed: {}", result.error().message);
        return EXIT_FAILURE;
    }
    std::vector<unsigned char> payload(config.bytes);
    for (std::size_t index = 0; index < payload.size(); ++index) {
        payload[index] = static_cast<unsigned char>(index ^ 0x5aU);
    }
    nds::client::DeviceBuffer data;
    if (const auto result = client.allocate(payload.size(), &data); !result) {
        NDS_LOG_ERROR("npu-client", "client application buffer allocation failed: {}", result.error().message);
        return EXIT_FAILURE;
    }
    if (config.operation == "write") {
        if (const auto copied = client.copy_to_device(&data, payload.data(), payload.size()); !copied) {
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
        std::vector<unsigned char> result(payload.size());
        if (const auto copied = client.copy_from_device(result.data(), data, result.size()); !copied) {
            NDS_LOG_ERROR("npu-client", "client Read copy failed: {}", copied.error().message);
            return EXIT_FAILURE;
        }
        if (result != std::vector<unsigned char>(result.size(), 0U)) {
            NDS_LOG_ERROR("npu-client", "storage Read verification failed");
            return EXIT_FAILURE;
        }
    }
    NDS_LOG_INFO("npu-client", "CPU completed the NDS storage command.");
    return EXIT_SUCCESS;
}

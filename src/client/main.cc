#include "nds/logging.hh"
#include "nds/protocol.h"
#include "nds/result.hh"
#include "protocol.hh"
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
    nds::client::ConnectionConfig connection;
    std::string operation{"write"};
    std::uint64_t offset{};
    std::uint32_t bytes{4096U};
    std::string log_sink{"stderr"};
    std::string log_level{"info"};
};

nds::Result<int> parse_args(int argc, char **argv, ClientConfig *config, bool *exit_requested) {
    if (config == nullptr || exit_requested == nullptr)
        return nds::failure(nds::ErrorCode::kInvalidArgument, "client configuration and exit state are required");
    CLI::App app{"Send one NDS storage command from an NPU to a CPU memory namespace."};
    app.add_option("--ascendcl", config->connection.context.ascendcl_library, "AscendCL shared library")->required();
    app.add_option("--runtime", config->connection.context.runtime_library, "CANN runtime shared library")->required();
    app.add_option("--ra", config->connection.context.ra_library, "CANN RA shared library")->required();
    std::string backend{"host-ra"};
    app.add_option("--backend", backend, "NPU RDMA backend")->check(CLI::IsMember({"host-ra", "aicpu", "aiv"}));
    app.add_option("--aicpu-kernel-config", config->connection.backend.aicpu_kernel_config,
                   "AICPU kernel package configuration");
    app.add_option("--aiv-kernel", config->connection.backend.aiv_kernel, "AIV kernel binary");
    app.add_option("--operation", config->operation, "Storage operation")->check(CLI::IsMember({"read", "write"}));
    app.add_option("--offset", config->offset, "Namespace byte offset");
    app.add_option("--bytes", config->bytes, "Storage transfer length")
        ->check(CLI::Range(std::uint32_t{1}, static_cast<std::uint32_t>(kMaxTransferBytes)));
    app.add_option("--npu-ip", config->connection.qp.local_ipv4, "NPU RoCE IPv4 address")->required();
    app.add_option("--logical-device", config->connection.context.logical_device_id, "NPU logical device")->required();
    app.add_option("--physical-device", config->connection.context.physical_device_id, "NPU physical device")
        ->required();
    app.add_option("--port", config->connection.qp.port_num, "NPU RoCE port")
        ->check(CLI::Range(std::uint16_t{1}, std::numeric_limits<std::uint16_t>::max()));
    app.add_option("--path-mtu", config->connection.qp.path_mtu, "Path MTU")
        ->check(CLI::Range(std::uint16_t{1}, std::numeric_limits<std::uint16_t>::max()));
    app.add_option("--cpu-ip", config->connection.cpu_ipv4, "CPU peer IPv4 address")->required();
    app.add_option("--tcp-port", config->connection.tcp_port, "TCP peer-exchange port")
        ->check(CLI::Range(std::uint16_t{1}, std::numeric_limits<std::uint16_t>::max()));
    app.add_option("--tcp-timeout-ms", config->connection.tcp_timeout_ms, "TCP peer-exchange timeout")
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
        config->connection.backend.mode = nds::NpuBackendMode::Aicpu;
    if (backend == "aiv")
        config->connection.backend.mode = nds::NpuBackendMode::Aiv;
    config->connection.qp.physical_device_id = config->connection.context.physical_device_id;
    if ((backend == "aicpu" && config->connection.backend.aicpu_kernel_config.empty()) ||
        (backend == "aiv" && config->connection.backend.aiv_kernel.empty())) {
        return nds::failure(nds::ErrorCode::kInvalidArgument, "invalid option combination");
    }
    return nds::success(0);
}

}  // namespace

int main(int argc, char **argv) {
    (void)nds::log::configure("npu-client", "stderr", "info");
    ClientConfig config;
    config.connection.tcp_port = 18515U;
    config.connection.tcp_timeout_ms = 10000U;
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

    nds::client::Connection connection;
    if (const auto result = connection.open(config.connection); !result) {
        NDS_LOG_ERROR("npu-client", "client connection failed: {}", result.error().message);
        return EXIT_FAILURE;
    }
    std::vector<unsigned char> payload(config.bytes);
    for (std::size_t index = 0; index < payload.size(); ++index) {
        payload[index] = static_cast<unsigned char>(index ^ 0x5aU);
    }
    nds::client::DeviceBuffer data;
    if (const auto result = connection.allocate(payload.size(), &data); !result) {
        NDS_LOG_ERROR("npu-client", "client application buffer allocation failed: {}", result.error().message);
        return EXIT_FAILURE;
    }
    if (const auto result = connection.copy_to_device(&data, payload.data(), payload.size()); !result) {
        NDS_LOG_ERROR("npu-client", "client application buffer copy failed: {}", result.error().message);
        return EXIT_FAILURE;
    }

    const std::uint16_t operation = config.operation == "read" ? NDS_PROTOCOL_READ : NDS_PROTOCOL_WRITE;
    const nds_transport_endpoint &local = connection.local_endpoint();
    const std::uint64_t request_id = (static_cast<std::uint64_t>(local.qp_num) << 32U) | local.psn;
    if (const auto result =
            nds::client::execute_request(&connection, {request_id, operation, config.offset, config.bytes, &data});
        !result) {
        NDS_LOG_ERROR("npu-client", "protocol request failed: {}", result.error().message);
        return EXIT_FAILURE;
    }
    if (operation == NDS_PROTOCOL_READ) {
        std::vector<unsigned char> result(payload.size());
        if (const auto copied = connection.copy_from_device(result.data(), data, result.size()); !copied) {
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

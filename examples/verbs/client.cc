#include "logging.hh"
#include "runtime.hh"
#include "transport.hh"
#include "wire.hh"

#include <CLI/CLI.hpp>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <string>
#include <utility>

namespace {

struct Config {
    nds::client::RuntimeConfig runtime;
    nds::client::EndpointConfig endpoint;
    nds::client::NpuBackend backend{nds::client::NpuBackend::Ra};
    std::string ra_backend;
    std::string aiv_kernel;
    std::string aicpu_kernel;
    std::string server;
};

nds::Result<Config> parse(int argc, char **argv) {
    Config config;
    CLI::App app{"Exercise direct NPU verbs with a TCP QP bootstrap."};
    app.add_option("--cann-runtime", config.runtime.cann_runtime_library)->required();
    app.add_option("--ra", config.endpoint.ra_library)->required();
    app.add_option("--ra-backend", config.ra_backend);
    app.add_option("--logical-device", config.runtime.logical_device_id)->required();
    app.add_option("--server", config.server)->required();
    std::string backend_name{"ra"};
    app.add_option("--backend", backend_name, "Backend: ra, aiv, or aicpu");
    app.add_option("--aiv-kernel", config.aiv_kernel);
    app.add_option("--aicpu-kernel", config.aicpu_kernel);
    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError &error) {
        return Error{nds::ErrorCode::kInvalidArgument, app.exit(error) == 0 ? "help requested" : "invalid options"};
    }
    if (backend_name == "ra")
        config.backend = nds::client::NpuBackend::Ra;
    else if (backend_name == "aiv")
        config.backend = nds::client::NpuBackend::Aiv;
    else if (backend_name == "aicpu")
        config.backend = nds::client::NpuBackend::Aicpu;
    else
        return Error{nds::ErrorCode::kInvalidArgument, "--backend must be ra, aiv, or aicpu"};
    if (config.backend == nds::client::NpuBackend::Aiv && config.aiv_kernel.empty())
        return Error{nds::ErrorCode::kInvalidArgument, "--aiv-kernel is required for AIV"};
    if (config.backend == nds::client::NpuBackend::Aicpu && config.aicpu_kernel.empty())
        return Error{nds::ErrorCode::kInvalidArgument, "--aicpu-kernel is required for AICPU"};
    if (config.backend == nds::client::NpuBackend::Ra && config.ra_backend.empty())
        return Error{nds::ErrorCode::kInvalidArgument, "--ra-backend is required for RA"};
    return config;
}

}  // namespace

int main(int argc, char **argv) {
    (void)nds::log::configure("verbs-client", "stderr", "info");
    const nds::Result<Config> config_result = parse(argc, argv);
    if (!config_result.ok()) {
        NDS_LOG_ERROR("verbs-client", "options failed: {}", config_result.error().message);
        return EXIT_FAILURE;
    }
    const Config &config = config_result.value();
    nds::client::Runtime runtime;
    const nds::Result<void> runtime_open_result = runtime.open(config.runtime);
    if (!runtime_open_result.ok()) {
        NDS_LOG_ERROR("verbs-client", "runtime open failed: {}", runtime_open_result.error().message);
        return EXIT_FAILURE;
    }
    // One QP and one MR: resource policy belongs to the transport, not the caller.
    const nds::client::QueuePairConfig qp_config{
        .port_num = 1U,
        .path_mtu = 1024U,
        .traffic_class = 0U,
        .service_level = 0U,
        .retry_count = 7U,
        .retry_timeout = 14U,
        .ai_qp_mode = -1,
        .send_queue_depth = 32768U,
        .receive_queue_depth = 128U,
        .control_flags = 0U,
    };
    const nds::client::TransportConfig transport_config{config.endpoint, qp_config, 1U, config.server, 5000U};
    const nds::client::BackendConfig backend_config{config.backend, config.ra_backend, config.aicpu_kernel,
                                                    config.aiv_kernel};
    nds::client::Transport transport;
    const nds::Result<void> transport_open_result = transport.open(&runtime, transport_config, backend_config);
    if (!transport_open_result.ok()) {
        NDS_LOG_ERROR("verbs-client", "transport open failed: {}", transport_open_result.error().message);
        return EXIT_FAILURE;
    }
    // Register exactly one device MR and use it for the Send WR.
    std::array<std::byte, 64U> payload{};
    const nds::Result<nds::client::MemoryBuffer> payload_buffer_result = runtime.allocate(payload.size());
    if (!payload_buffer_result.ok()) {
        NDS_LOG_ERROR("verbs-client", "NPU allocation failed: {}", payload_buffer_result.error().message);
        return EXIT_FAILURE;
    }
    const nds::Result<nds::client::MemoryRegion> payload_region_result =
        transport.register_memory(payload_buffer_result.value(), nds::client::MemoryAccess::DirectNpu);
    if (!payload_region_result.ok()) {
        NDS_LOG_ERROR("verbs-client", "memory registration failed: {}", payload_region_result.error().message);
        return EXIT_FAILURE;
    }

    const nds::Result<nds::client::QueueHandle> queue_result = transport.queue(0U);
    if (!queue_result.ok())
        return EXIT_FAILURE;
    const nds::client::TransportSend request{&payload_region_result.value(), static_cast<std::uint32_t>(payload.size()),
                                             0U};
    if (const nds::Result<void> submitted = transport.send(queue_result.value(), request); !submitted) {
        NDS_LOG_ERROR("verbs-client", "Send failed: {}", submitted.error().message);
        return EXIT_FAILURE;
    }
    NDS_LOG_INFO("verbs-client", "submitted verbs Send");
    return EXIT_SUCCESS;
}

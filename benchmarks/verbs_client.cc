#include "verbs_wire.hh"
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
    std::string server;
    nds::client::BackendMode backend{nds::client::BackendMode::Ra};
    std::string backend_artifact_path;
};

nds::Result<Config> parse(int argc, char **argv) {
    Config config;
    CLI::App app{"Run the direct one-QP, one-MR NPU verbs benchmark."};
    app.add_option("--backend-artifact-path", config.backend_artifact_path);
    app.add_option("--logical-device", config.runtime.logical_device_id)->required();
    app.add_option("--server", config.server)->required();
    std::string backend_mode_name{"ra"};
    app.add_option("--backend-mode", backend_mode_name, "Backend: ra, aiv, or aicpu");
    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError &error) {
        return Error{nds::ErrorCode::kInvalidArgument, app.exit(error) == 0 ? "help requested" : "invalid options"};
    }
    if (backend_mode_name == "ra")
        config.backend = nds::client::BackendMode::Ra;
    else if (backend_mode_name == "aiv")
        config.backend = nds::client::BackendMode::Aiv;
    else if (backend_mode_name == "aicpu")
        config.backend = nds::client::BackendMode::Aicpu;
    else
        return Error{nds::ErrorCode::kInvalidArgument, "--backend-mode must be ra, aiv, or aicpu"};
    if (config.backend_artifact_path.empty())
        return Error{nds::ErrorCode::kInvalidArgument, "--backend-artifact-path is required"};
    return config;
}

}  // namespace

int main(int argc, char **argv) {
    (void)nds::log::configure("verbs-benchmark-client", "stderr", "info");
    const nds::Result<Config> config_result = parse(argc, argv);
    if (!config_result.ok())
        return EXIT_FAILURE;
    const Config &config = config_result.value();
    nds::client::Runtime runtime;
    const nds::Result<void> runtime_open_result = runtime.open(config.runtime);
    if (!runtime_open_result.ok())
        return EXIT_FAILURE;
    // One QP and one MR: resource policy belongs to the transport.
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
    const nds::client::BackendConfig backend_config{config.backend, config.backend_artifact_path};
    nds::client::Transport transport;
    if (!transport.open(&runtime, transport_config, backend_config).ok())
        return EXIT_FAILURE;
    // Register one device buffer; setup is outside the operation itself.
    std::array<std::byte, 64U> payload{};
    const nds::Result<nds::client::MemoryBuffer> payload_buffer_result =
        runtime.allocate(payload.size(), nds::client::MemoryLocation::Device);
    if (!payload_buffer_result.ok())
        return EXIT_FAILURE;
    const nds::Result<nds::client::MemoryRegion> payload_region_result =
        transport.register_memory(payload_buffer_result.value(), nds::client::MemoryAccess::DirectNpu);
    if (!payload_region_result.ok())
        return EXIT_FAILURE;
    const nds::Result<nds::client::QueueHandle> queue_result = transport.queue(0U);
    if (!queue_result.ok())
        return EXIT_FAILURE;
    const nds::client::TransportSend request{&payload_region_result.value(), static_cast<std::uint32_t>(payload.size()),
                                             0U};
    if (!transport.send(queue_result.value(), request).ok())
        return EXIT_FAILURE;
    NDS_LOG_INFO("verbs-benchmark-client", "completed {} verbs operation",
                 nds::benchmark::operation_name(nds::benchmark::VerbsOperation::Send));
    return EXIT_SUCCESS;
}

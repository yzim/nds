#include "aicpu/host/launcher.hh"
#include "aiv/host/launcher.hh"
#include "nds/logging.hh"
#include "ra/ra.hh"
#include "runtime.hh"
#include "transport.hh"

#include <CLI/CLI.hpp>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <string>

namespace {

struct Config {
    nds::client::RuntimeConfig runtime;
    nds::client::TransportConfig transport;
    nds::client::ExecutionConfig execution;
    std::string layer;
};

nds::Result<void> parse(int argc, char **argv, Config *config) {
    if (config == nullptr)
        return nds::unexpected(nds::ErrorCode::kInvalidArgument, "layer probe configuration is required");
    CLI::App app{"Exercise one NDS verbs or transport Send against the CPU probe receiver."};
    app.add_option("--layer", config->layer, "Layer to exercise")->required()->check(CLI::IsMember({"verbs", "transport"}));
    std::string execution{"ra"};
    app.add_option("--backend", execution, "Execution backend")->required()->check(CLI::IsMember({"ra", "aiv", "aicpu"}));
    app.add_option("--ascendcl", config->runtime.ascendcl_library)->required();
    app.add_option("--runtime", config->runtime.runtime_library)->required();
    app.add_option("--ra", config->runtime.ra_library)->required();
    app.add_option("--aiv-kernel", config->execution.aiv_kernel);
    app.add_option("--aicpu-kernel", config->execution.aicpu_kernel);
    app.add_option("--npu-ip", config->transport.qp.local_ipv4)->required();
    app.add_option("--logical-device", config->runtime.logical_device_id)->required();
    app.add_option("--physical-device", config->runtime.physical_device_id)->required();
    app.add_option("--cpu-ip", config->transport.cpu_ipv4)->required();
    app.add_option("--tcp-port", config->transport.tcp_port)->required();
    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError &error) {
        return nds::unexpected(nds::ErrorCode::kInvalidArgument, app.exit(error) == 0 ? "help requested" : "invalid options");
    }
    config->transport.qp.physical_device_id = config->runtime.physical_device_id;
    if (execution == "aiv") config->execution.mode = nds::NpuExecutionMode::Aiv;
    if (execution == "aicpu") config->execution.mode = nds::NpuExecutionMode::Aicpu;
    if ((config->execution.mode == nds::NpuExecutionMode::Aiv && config->execution.aiv_kernel.empty()) ||
        (config->execution.mode == nds::NpuExecutionMode::Aicpu && config->execution.aicpu_kernel.empty())) {
        return nds::unexpected(nds::ErrorCode::kInvalidArgument, "device backend requires its kernel artifact");
    }
    return {};
}

nds::Result<void> post_verbs(nds::client::NpuRuntime *runtime, nds::client::Transport *transport,
                             const std::array<unsigned char, 64U> &payload) {
    nds::client::DeviceBuffer buffer;
    nds::client::RegisteredRegion region;
    auto *memory = runtime->memory();
    if (const auto allocated = memory->allocate(payload.size(), &buffer); !allocated) return nds::unexpected(allocated.error());
    if (const auto copied = memory->copy_to_device(&buffer, payload.data(), payload.size()); !copied) return nds::unexpected(copied.error());
    if (const auto registered = memory->register_memory(transport->qp(), &buffer, &region); !registered) return nds::unexpected(registered.error());
    const auto local = region.local_address();
    const nds_device_send_wr wr{1U, NDS_DEVICE_WR_SEND, NDS_DEVICE_SEND_SIGNALED,
                                {local.address, static_cast<std::uint32_t>(payload.size()), local.key}, 0U, 0U, 0U};
    if (transport->execution().mode == nds::NpuExecutionMode::Ra) {
        const auto posted = nds::NdsRaPostSend(transport->qp(), wr);
        if (!posted) return nds::unexpected(posted.error());
        if (!runtime->context()->ring_rdma_doorbell(posted->doorbell.db_index, posted->doorbell.db_info))
            return nds::unexpected(nds::ErrorCode::kRuntime, runtime->context()->error());
        return {};
    }
    nds::client::DeviceBuffer result_buffer;
    if (const auto allocated = memory->allocate(sizeof(nds_device_operation_result), &result_buffer); !allocated)
        return nds::unexpected(allocated.error());
    const nds_device_operation_result pending{NDS_DEVICE_OPERATION_INVALID_ARGUMENT, NDS_DEVICE_OPERATION_PATH_NONE, 0, 0U};
    if (const auto copied = memory->copy_to_device(&result_buffer, &pending, sizeof(pending)); !copied) return nds::unexpected(copied.error());
    nds_device_post_send_request request{};
    const auto device_transport = transport->qp()->make_device_transport();
    if (!device_transport) return nds::unexpected(device_transport.error());
    request.qp = device_transport->control_qp;
    request.wr = wr;
    request.operation_result_address = reinterpret_cast<std::uint64_t>(result_buffer.data());
    std::string error;
    if (transport->execution().mode == nds::NpuExecutionMode::Aicpu) {
        nds::AicpuEntrypointLauncher launcher;
        if (!launcher.load(&runtime->context()->acl_api(), transport->execution().aicpu_kernel) ||
            !launcher.launch_post_send_and_wait(&request, 5000)) error = launcher.error();
    } else {
        nds::AivEntrypointLauncher launcher;
        nds::client::DeviceBuffer request_buffer;
        request.abi_version = NDS_DEVICE_OPERATOR_ARGS_ABI_VERSION;
        request.size = sizeof(request);
        if (!launcher.load(&runtime->context()->acl_api(), transport->execution().aiv_kernel) ||
            !(memory->allocate(sizeof(request), &request_buffer)) ||
            !(memory->copy_to_device(&request_buffer, &request, sizeof(request))) ||
            !launcher.launch_post_send_and_wait(reinterpret_cast<std::uint64_t>(request_buffer.data()), 5000)) error = launcher.error();
    }
    if (!error.empty()) return nds::unexpected(nds::ErrorCode::kRuntime, error);
    nds_device_operation_result completed{};
    if (const auto copied = memory->copy_from_device(&completed, result_buffer, sizeof(completed)); !copied) return nds::unexpected(copied.error());
    return completed.status == NDS_DEVICE_OPERATION_SUCCESS ? nds::Result<void>{}
                                                           : nds::unexpected(nds::ErrorCode::kRuntime, "device verbs Send failed");
}

}  // namespace

int main(int argc, char **argv) {
    (void)nds::log::configure("npu-client", "stderr", "info");
    Config config;
    const auto parsed = parse(argc, argv, &config);
    if (!parsed) {
        NDS_LOG_ERROR("npu-client", "layer probe options failed: {}", parsed.error().message);
        return EXIT_FAILURE;
    }
    nds::client::NpuRuntime runtime;
    nds::client::Transport transport;
    if (const auto opened = runtime.open(config.runtime); !opened) {
        NDS_LOG_ERROR("npu-client", "layer probe runtime open failed: {}", opened.error().message);
        return EXIT_FAILURE;
    }
    if (const auto opened = transport.open(&runtime, config.transport, config.execution); !opened) {
        NDS_LOG_ERROR("npu-client", "layer probe transport open failed: {}", opened.error().message);
        return EXIT_FAILURE;
    }
    const std::array<unsigned char, 64U> payload{0x4eU, 0x44U, 0x53U};
    const auto result = config.layer == "verbs" ? post_verbs(&runtime, &transport, payload)
                                                   : transport.send_bytes(payload.data(), payload.size());
    if (!result) {
        NDS_LOG_ERROR("npu-client", "{} layer probe failed: {}", config.layer, result.error().message);
        return EXIT_FAILURE;
    }
    NDS_LOG_INFO("npu-client", "{} layer probe completed", config.layer);
    return EXIT_SUCCESS;
}

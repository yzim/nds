#include "aicpu/host/launcher.hh"
#include "aiv/host/launcher.hh"
#include "nds/logging.hh"
#include "ra/ra.hh"
#include "runtime.hh"
#include "transport.hh"

#include <CLI/CLI.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>

namespace {

struct Config {
    nds::client::RuntimeConfig runtime;
    nds::client::TransportConfig transport;
    nds::client::ExecutionConfig execution;
};

nds::Result<Config> parse(int argc, char **argv) {
    Config config;
    CLI::App app{"Exercise one NDS transport Send."};
    std::string backend{"ra"};
    app.add_option("--backend", backend, "Execution backend")
        ->required()
        ->check(CLI::IsMember({"ra", "aiv", "aicpu"}));
    app.add_option("--ascendcl", config.runtime.ascendcl_library)->required();
    app.add_option("--runtime", config.runtime.runtime_library)->required();
    app.add_option("--ra", config.transport.endpoint.ra_library)->required();
    app.add_option("--aiv-kernel", config.execution.aiv_kernel);
    app.add_option("--aicpu-kernel-config", config.execution.aicpu_kernel_config);
    app.add_option("--logical-device", config.runtime.logical_device_id)->required();
    app.add_option("--server", config.transport.server_address)->required();
    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError &error) {
        return nds::unexpected(nds::ErrorCode::kInvalidArgument,
                               app.exit(error) == 0 ? "help requested" : "invalid options");
    }
    if (backend == "aiv")
        config.execution.mode = nds::client::NpuExecutionMode::Aiv;
    if (backend == "aicpu")
        config.execution.mode = nds::client::NpuExecutionMode::Aicpu;
    if ((config.execution.mode == nds::client::NpuExecutionMode::Aiv && config.execution.aiv_kernel.empty()) ||
        (config.execution.mode == nds::client::NpuExecutionMode::Aicpu &&
         config.execution.aicpu_kernel_config.empty())) {
        return nds::unexpected(nds::ErrorCode::kInvalidArgument, "device backend requires its kernel artifact");
    }
    return config;
}

nds::Result<void> send(nds::client::Runtime *runtime, nds::client::Transport *transport,
                       const std::array<std::byte, 64U> &payload) {
    auto allocated = runtime->allocate(payload.size());
    if (!allocated)
        return nds::unexpected(allocated.error());
    nds::client::MemoryBuffer buffer = std::move(*allocated);
    if (const auto copied = runtime->copy_to(&buffer, payload.data(), payload.size()); !copied)
        return nds::unexpected(copied.error());
    auto registered = transport->endpoint()->reg_mr(buffer, nds::client::MemoryAccess::DirectNpu);
    if (!registered)
        return nds::unexpected(registered.error());
    nds::client::MemoryRegion region = std::move(*registered);
    const NdsDeviceTransfer transfer{
        1U, {region.address(), static_cast<std::uint32_t>(payload.size()), region.local_key()}, 0U, 0U, 0U};
    if (transport->execution().mode == nds::client::NpuExecutionMode::Ra)
        return nds::NdsRaRdmaSend({runtime, transport->qp()}, transfer);

    const auto device_transport = transport->qp()->make_device_transport();
    if (!device_transport)
        return nds::unexpected(device_transport.error());
    auto allocated_result = runtime->allocate(sizeof(NdsDeviceOperationResult));
    if (!allocated_result)
        return nds::unexpected(allocated_result.error());
    nds::client::MemoryBuffer result_buffer = std::move(*allocated_result);
    const NdsDeviceOperationResult pending{NDS_DEVICE_OPERATION_INVALID_ARGUMENT, NDS_DEVICE_OPERATION_PATH_NONE, 0,
                                              0U};
    if (const auto copied = runtime->copy_to(&result_buffer, &pending, sizeof(pending)); !copied)
        return nds::unexpected(copied.error());
    NdsDeviceOperationRequest request{};
    request.transport = *device_transport;
    request.operation = NDS_DEVICE_RDMA_SEND;
    request.parameters.transfer = transfer;
    request.operation_result_address = reinterpret_cast<std::uint64_t>(result_buffer.data());
    if (transport->execution().mode == nds::client::NpuExecutionMode::Aicpu) {
        nds::AicpuEntrypointLauncher launcher;
        if (const auto loaded = launcher.load(&runtime->acl_api(), transport->execution().aicpu_kernel_config); !loaded)
            return nds::unexpected(loaded.error());
        if (const auto launched = launcher.launch_and_wait(&request, 5000); !launched)
            return nds::unexpected(launched.error());
    } else {
        nds::AivEntrypointLauncher launcher;
        if (const auto loaded = launcher.load(&runtime->acl_api(), transport->execution().aiv_kernel); !loaded)
            return nds::unexpected(loaded.error());
        auto device_request = launcher.make_device_request(request);
        if (!device_request)
            return nds::unexpected(device_request.error());
        auto request_buffer = runtime->allocate(sizeof(*device_request));
        if (!request_buffer)
            return nds::unexpected(request_buffer.error());
        if (const auto copied = runtime->copy_to(&*request_buffer, &*device_request, sizeof(*device_request)); !copied)
            return nds::unexpected(copied.error());
        if (const auto launched = launcher.launch_and_wait(reinterpret_cast<std::uint64_t>(request_buffer->data()),
                                                           request.operation, 5000);
            !launched) {
            return nds::unexpected(launched.error());
        }
    }
    NdsDeviceOperationResult completed{};
    if (const auto copied = runtime->copy_from(&completed, result_buffer, sizeof(completed)); !copied)
        return nds::unexpected(copied.error());
    return completed.status == NDS_DEVICE_OPERATION_SUCCESS
               ? nds::Result<void>{}
               : nds::unexpected(nds::ErrorCode::kRuntime, "device transport Send failed");
}

}  // namespace

int main(int argc, char **argv) {
    (void)nds::log::configure("transport-client", "stderr", "info");
    const auto parsed = parse(argc, argv);
    if (!parsed) {
        NDS_LOG_ERROR("transport-client", "options failed: {}", parsed.error().message);
        return EXIT_FAILURE;
    }
    nds::client::Runtime runtime;
    nds::client::Transport transport;
    if (const auto opened = runtime.open(parsed->runtime); !opened) {
        NDS_LOG_ERROR("transport-client", "runtime open failed: {}", opened.error().message);
        return EXIT_FAILURE;
    }
    if (const auto opened = transport.open(&runtime, parsed->transport, parsed->execution); !opened) {
        NDS_LOG_ERROR("transport-client", "transport open failed: {}", opened.error().message);
        return EXIT_FAILURE;
    }
    const std::array<std::byte, 64U> payload{std::byte{0x4e}, std::byte{0x44}, std::byte{0x53}};
    if (const auto sent = send(&runtime, &transport, payload); !sent) {
        NDS_LOG_ERROR("transport-client", "Send failed: {}", sent.error().message);
        return EXIT_FAILURE;
    }
    NDS_LOG_INFO("transport-client", "completed one NDS transport Send");
    return EXIT_SUCCESS;
}

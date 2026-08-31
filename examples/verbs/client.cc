#include "backends/launcher.hh"
#include "backends/backend_mode.hh"
#include "endpoint.hh"
#include "logging.hh"
#include "runtime.hh"
#include "tcp_socket.hh"
#include "transport_protocol.hh"
#include "wire.hh"

#include <CLI/CLI.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <string>
#include <thread>
#include <utility>

namespace {

struct Config {
    nds::client::RuntimeConfig runtime;
    nds::client::EndpointConfig endpoint;
    nds::client::BackendMode backend{nds::client::BackendMode::Ra};
    std::string backend_artifact;
    std::string server;
};

nds::Result<Config> parse(int argc, char **argv) {
    Config config;
    CLI::App app{"Exercise direct NPU verbs with a TCP QP bootstrap."};
    app.add_option("--cann-runtime", config.runtime.cann_runtime_library)->required();
    app.add_option("--ra", config.endpoint.ra_library)->required();
    app.add_option("--backend-artifact", config.backend_artifact,
                   "RA backend shared artifact, AIV kernel object, or AICPU package descriptor");
    app.add_option("--logical-device", config.runtime.logical_device_id)->required();
    app.add_option("--server", config.server)->required();
    std::string backend_mode_name{"ra"};
    app.add_option("--backend-mode", backend_mode_name, "Backend: ra, aiv, or aicpu");
    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError &error) {
        return nds::Error{nds::ErrorCode::kInvalidArgument,
                          app.exit(error) == 0 ? "help requested" : "invalid options"};
    }
    if (backend_mode_name == "ra")
        config.backend = nds::client::BackendMode::Ra;
    else if (backend_mode_name == "aiv")
        config.backend = nds::client::BackendMode::Aiv;
    else if (backend_mode_name == "aicpu")
        config.backend = nds::client::BackendMode::Aicpu;
    else
        return nds::Error{nds::ErrorCode::kInvalidArgument, "--backend-mode must be ra, aiv, or aicpu"};
    if (config.backend_artifact.empty())
        return nds::Error{nds::ErrorCode::kInvalidArgument, "--backend-artifact is required"};
    return config;
}

nds::Result<void> run(int argc, char **argv) {
    NDS_ASSIGN_OR_RETURN(Config config, parse(argc, argv));
    NDS_ASSIGN_OR_RETURN(nds::TcpAddress address, nds::parse_tcp_address(config.server));

    nds::client::Runtime runtime;
    NDS_RETURN_IF_ERROR(runtime.open(config.runtime));

    // This example owns exactly one QP and one MR.
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
    NDS_ASSIGN_OR_RETURN(nds::client::Endpoint endpoint, runtime.create_endpoint(config.endpoint));
    NDS_ASSIGN_OR_RETURN(nds::client::QueuePair queue_pair, endpoint.create_qp(qp_config, config.backend));

    // Register exactly one device MR and use it for the Send WR.
    std::array<std::byte, 64U> payload{};
    NDS_ASSIGN_OR_RETURN(nds::client::MemoryBuffer payload_buffer, runtime.allocate(payload.size()));
    NDS_ASSIGN_OR_RETURN(nds::client::MemoryRegion payload_region,
                         endpoint.reg_mr(payload_buffer, nds::client::MemoryAccess::DirectNpu));

    // The launcher owns backend launch details; the example keeps only the QP.
    nds::client::BackendLauncher launcher;
    NDS_RETURN_IF_ERROR(launcher.open(&runtime, config.backend, config.backend_artifact));

    aclrtStream stream{};
    const int stream_result = aclrtCreateStream(&stream);
    if (stream_result != ACL_SUCCESS || stream == nullptr)
        return nds::Error{nds::ErrorCode::kRuntime, "aclrtCreateStream failed: " + std::to_string(stream_result)};
    struct StreamGuard {
        aclrtStream stream{};
        ~StreamGuard() {
            if (stream != nullptr)
                (void)aclrtDestroyStream(stream);
        }
    } stream_guard{stream};
    const nds::client::LaunchConfig launch_config{1U, nullptr, stream, nullptr, 0U};
    NDS_ASSIGN_OR_RETURN(NdsDeviceQp device_qp, launcher.describe_qp(queue_pair));

    // TCP starts only after the local RoCE runtime, endpoint, QP, and MR exist.
    NDS_ASSIGN_OR_RETURN(nds::TcpConnection connection, nds::TcpConnection::connect(address.ipv4, address.port, 5000U));
    NDS_ASSIGN_OR_RETURN(nds::transport::QpInfo local_qp_info, queue_pair.local_qp_info());
    NDS_ASSIGN_OR_RETURN(nds::transport::QpInfo peer_qp_info,
                         nds::examples::verbs::exchange_client_qp(connection, local_qp_info));
    NDS_RETURN_IF_ERROR(queue_pair.connect(peer_qp_info));
    NDS_RETURN_IF_ERROR(nds::examples::verbs::wait_ready(connection));

    const NdsDeviceSendWr send_wr{
        1U,
        NDS_DEVICE_WR_SEND,
        NDS_DEVICE_SEND_SIGNALED,
        {payload_region.address(), static_cast<std::uint32_t>(payload.size()), payload_region.local_key()},
        0U,
        0U,
        0U};
    NDS_RETURN_IF_ERROR(launcher.post_send(launch_config, device_qp, send_wr, 5000));

    const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        NdsDeviceWc completion{};
        NDS_ASSIGN_OR_RETURN(std::uint32_t completion_count,
                             launcher.poll_cq(launch_config, device_qp, true, 1U, &completion, 5000));
        if (completion_count != 0U) {
            if (completion.status != NDS_DEVICE_WC_SUCCESS)
                return nds::Error{nds::ErrorCode::kRa, "Send completion failed"};
            return {};
        }
        std::this_thread::yield();
    }
    return nds::Error{nds::ErrorCode::kRuntime, "Send completion timed out"};
}

}  // namespace

int main(int argc, char **argv) {
    (void)nds::log::configure("verbs-client", "stderr", "info");
    const nds::Result<void> run_result = run(argc, argv);
    if (!run_result.ok()) {
        NDS_LOG_ERROR("verbs-client", "verbs example failed: {}", run_result.error().message);
        return EXIT_FAILURE;
    }
    NDS_LOG_INFO("verbs-client", "submitted verbs Send");
    return EXIT_SUCCESS;
}

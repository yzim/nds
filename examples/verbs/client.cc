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
    std::string backend_artifact_path;
    std::string server;
};

enum class Operation {
    Send,
    Receive,
    Read,
    Write,
};

nds::Result<Config> parse(int argc, char **argv) {
    Config config;
    CLI::App app{"Exercise direct NPU verbs with a TCP QP bootstrap."};
    std::string backend_mode_name{"ra"};
    app.add_option("--backend-mode", backend_mode_name, "Backend: ra, aiv, or aicpu");
    app.add_option("--backend-artifact-path", config.backend_artifact_path,
                   "RA backend shared artifact, AIV kernel object, or AICPU package descriptor");
    app.add_option("--logical-device", config.runtime.logical_device_id)->required();
    app.add_option("--server", config.server)->required();
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
    if (config.backend_artifact_path.empty())
        return nds::Error{nds::ErrorCode::kInvalidArgument, "--backend-artifact-path is required"};
    return config;
}

template <typename LauncherView>
nds::Result<void> poll_completion(const LauncherView &launcher, const NdsQpDescriptor &device_qp, bool send_cq) {
    const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        NdsWc completion{};
        NDS_ASSIGN_OR_RETURN(std::uint32_t completion_count, launcher.poll_cq(device_qp, send_cq, 1U, &completion));
        if (completion_count == 0U) {
            std::this_thread::yield();
            continue;
        }
        if (completion.status != NDS_WC_SUCCESS)
            return nds::Error{nds::ErrorCode::kRa, "CQ completion failed"};
        return {};
    }
    return nds::Error{nds::ErrorCode::kRuntime, "CQ completion timed out"};
}

nds::Result<void> run_send(nds::client::Launcher *launcher, const NdsQpDescriptor &device_qp,
                           const nds::client::MemoryRegion &payload_region, std::uint32_t payload_length,
                           nds::TcpConnection *connection, aclrtStream stream) {
    const NdsSendWr send_wr{
        .wr_id = 1U,
        .opcode = NDS_WR_SEND,
        .flags = NDS_SEND_SIGNALED,
        .local = {.address = payload_region.address(),
                  .length = payload_length,
                  .local_key = payload_region.local_key()},
        .remote_address = 0U,
        .remote_key = 0U,
    };
    NDS_RETURN_IF_ERROR(launcher
                            ->with_config({
                                .block_dim = 1U,
                                .stream = stream,
                                .sync = true,
                                .sync_timeout_ms = 5000,
                            })
                            .post_send(device_qp, send_wr));
    NDS_RETURN_IF_ERROR(poll_completion(launcher->with_config({
                                            .block_dim = 1U,
                                            .stream = stream,
                                            .sync = true,
                                            .sync_timeout_ms = 5000,
                                        }),
                                        device_qp, true));
    return nds::examples::verbs::wait_barrier(*connection);
}

nds::Result<void> run_receive(nds::client::Launcher *launcher, const NdsQpDescriptor &device_qp,
                              const nds::client::MemoryRegion &payload_region, std::uint32_t payload_length,
                              nds::TcpConnection *connection, aclrtStream stream) {
    const NdsRecvWr receive_wr{
        .wr_id = 2U,
        .local = {.address = payload_region.address(),
                  .length = payload_length,
                  .local_key = payload_region.local_key()},
    };
    NDS_RETURN_IF_ERROR(launcher
                            ->with_config({
                                .block_dim = 1U,
                                .stream = stream,
                                .sync = true,
                                .sync_timeout_ms = 5000,
                            })
                            .post_recv(device_qp, receive_wr));

    // The server sends only after this TCP acknowledgement, so the receive is armed first.
    NDS_RETURN_IF_ERROR(nds::examples::verbs::send_barrier(*connection));
    NDS_RETURN_IF_ERROR(poll_completion(launcher->with_config({
                                            .block_dim = 1U,
                                            .stream = stream,
                                            .sync = true,
                                            .sync_timeout_ms = 5000,
                                        }),
                                        device_qp, false));
    // The server sends this barrier after its return Send has completed.
    return nds::examples::verbs::wait_barrier(*connection);
}

nds::Result<void> run_write(nds::client::Launcher *launcher, const NdsQpDescriptor &device_qp,
                            const nds::client::MemoryRegion &payload_region, std::uint32_t payload_length,
                            nds::TcpConnection *connection, aclrtStream stream) {
    NDS_ASSIGN_OR_RETURN(nds::RemoteMemory remote_memory, nds::examples::verbs::receive_remote_memory(*connection));
    const NdsSendWr write_wr{
        .wr_id = 3U,
        .opcode = NDS_WR_RDMA_WRITE,
        .flags = NDS_SEND_SIGNALED,
        .local = {.address = payload_region.address(),
                  .length = payload_length,
                  .local_key = payload_region.local_key()},
        .remote_address = remote_memory.address,
        .remote_key = remote_memory.remote_key,
    };
    NDS_RETURN_IF_ERROR(launcher
                            ->with_config({
                                .block_dim = 1U,
                                .stream = stream,
                                .sync = true,
                                .sync_timeout_ms = 5000,
                            })
                            .post_send(device_qp, write_wr));
    NDS_RETURN_IF_ERROR(poll_completion(launcher->with_config({
                                            .block_dim = 1U,
                                            .stream = stream,
                                            .sync = true,
                                            .sync_timeout_ms = 5000,
                                        }),
                                        device_qp, true));
    return nds::examples::verbs::send_barrier(*connection);
}

nds::Result<void> run_read(nds::client::Launcher *launcher, const NdsQpDescriptor &device_qp,
                           const nds::client::MemoryRegion &payload_region, std::uint32_t payload_length,
                           nds::TcpConnection *connection, aclrtStream stream) {
    NDS_ASSIGN_OR_RETURN(nds::RemoteMemory remote_memory, nds::examples::verbs::receive_remote_memory(*connection));
    const NdsSendWr read_wr{
        .wr_id = 4U,
        .opcode = NDS_WR_RDMA_READ,
        .flags = NDS_SEND_SIGNALED,
        .local = {.address = payload_region.address(),
                  .length = payload_length,
                  .local_key = payload_region.local_key()},
        .remote_address = remote_memory.address,
        .remote_key = remote_memory.remote_key,
    };
    NDS_RETURN_IF_ERROR(launcher
                            ->with_config({
                                .block_dim = 1U,
                                .stream = stream,
                                .sync = true,
                                .sync_timeout_ms = 5000,
                            })
                            .post_send(device_qp, read_wr));
    NDS_RETURN_IF_ERROR(poll_completion(launcher->with_config({
                                            .block_dim = 1U,
                                            .stream = stream,
                                            .sync = true,
                                            .sync_timeout_ms = 5000,
                                        }),
                                        device_qp, true));
    return nds::examples::verbs::send_barrier(*connection);
}

nds::Result<void> run_operation(Operation operation, nds::client::Launcher *launcher, const NdsQpDescriptor &device_qp,
                                const nds::client::MemoryRegion &payload_region, std::uint32_t payload_length,
                                nds::TcpConnection *connection, aclrtStream stream) {
    switch (operation) {
        case Operation::Send:
            return run_send(launcher, device_qp, payload_region, payload_length, connection, stream);
        case Operation::Receive:
            return run_receive(launcher, device_qp, payload_region, payload_length, connection, stream);
        case Operation::Read:
            return run_read(launcher, device_qp, payload_region, payload_length, connection, stream);
        case Operation::Write:
            return run_write(launcher, device_qp, payload_region, payload_length, connection, stream);
    }
    return nds::Error{nds::ErrorCode::kInvalidArgument, "unsupported verbs operation"};
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
        // AI-QP CQ metadata is returned for caller-owned polling when requested.
        .control_flags =
            (config.backend == nds::client::BackendMode::Aiv || config.backend == nds::client::BackendMode::Aicpu)
                ? nds::client::QueuePairCallerPollsCq
                : 0U,
    };
    NDS_ASSIGN_OR_RETURN(nds::client::Endpoint endpoint, runtime.create_endpoint(config.endpoint));
    NDS_ASSIGN_OR_RETURN(nds::client::QueuePair queue_pair, endpoint.create_qp(qp_config, config.backend));

    // Register exactly one device MR and use it for the Send WR.
    std::array<std::byte, 64U> payload{};
    payload.fill(std::byte{0x5a});
    NDS_ASSIGN_OR_RETURN(nds::client::MemoryBuffer payload_buffer,
                         runtime.allocate(payload.size(), nds::client::MemoryLocation::Device));
    NDS_RETURN_IF_ERROR(runtime.copy_to(&payload_buffer, payload.data(), payload.size()));
    NDS_ASSIGN_OR_RETURN(
        nds::client::MemoryRegion payload_region,
        endpoint.reg_mr(payload_buffer, nds::client::MemoryAccess::LocalWrite | nds::client::MemoryAccess::RemoteWrite |
                                            nds::client::MemoryAccess::RemoteRead));

    // The launcher owns backend launch details; the example keeps only the QP.
    NDS_ASSIGN_OR_RETURN(std::unique_ptr<nds::client::Launcher> launcher,
                         nds::client::Launcher::open(&runtime, config.backend, config.backend_artifact_path));

    NDS_ASSIGN_OR_RETURN(NdsQpDescriptor device_qp, queue_pair.device_qp());

    // TCP starts only after the local RoCE runtime, endpoint, QP, and MR exist.
    NDS_ASSIGN_OR_RETURN(nds::TcpConnection connection, nds::TcpConnection::connect(address.ipv4, address.port, 5000U));
    NDS_ASSIGN_OR_RETURN(nds::QpInfo local_qp_info, queue_pair.local_qp_info());
    NDS_ASSIGN_OR_RETURN(nds::QpInfo peer_qp_info, nds::examples::verbs::exchange_client_qp(connection, local_qp_info));
    NDS_RETURN_IF_ERROR(queue_pair.connect(peer_qp_info));
    NDS_RETURN_IF_ERROR(nds::examples::verbs::wait_barrier(connection));

    // The caller owns one stream and passes it to every backend launch.
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

    const std::uint32_t payload_length = static_cast<std::uint32_t>(payload.size());
    for (const Operation operation : {Operation::Send, Operation::Receive, Operation::Read, Operation::Write})
        NDS_RETURN_IF_ERROR(
            run_operation(operation, launcher.get(), device_qp, payload_region, payload_length, &connection, stream));
    return {};
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

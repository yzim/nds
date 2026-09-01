#include "logging.hh"
#include "backends/launcher.hh"
#include "transport_protocol.hh"
#include "runtime.hh"
#include "transport.hh"

#include <CLI/CLI.hpp>

#include <cstddef>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace {
constexpr std::size_t kBytes = 64U;
constexpr char kHelpRequested[] = "help requested";
enum class Operation { Send, Recv, Read, Write };

struct Config {
    nds::client::RuntimeConfig runtime;
    nds::client::TransportConfig transport;
    nds::client::BackendConfig backend;
    Operation operation{Operation::Send};
};

nds::Result<Config> parse(int argc, char **argv) {
    Config config;
    std::string backend{"ra"};
    std::string operation{"send"};
    CLI::App app{"Exercise the NDS indexed transport request API."};
    app.add_option("--backend-mode", backend, "Backend mode: ra, aiv, or aicpu")
        ->required()
        ->check(CLI::IsMember({"ra", "aiv", "aicpu"}));
    app.add_option("--operation", operation, "Transport operation")
        ->check(CLI::IsMember({"send", "recv", "read", "write"}));
    app.add_option("--backend-artifact-path", config.backend.artifact_path,
                   "RA backend shared artifact, AIV kernel object, or AICPU package descriptor");
    app.add_option("--logical-device", config.runtime.logical_device_id, "NPU logical device")->required();
    app.add_option("--server", config.transport.server_address, "Server TCP exchange address as IPv4:port")->required();
    app.add_option("--qp-count", config.transport.qp_count, "Number of indexed transport queues")
        ->check(CLI::Range(1U, nds::wire::kMaxQpInfoBatch));
    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError &error) {
        return nds::Error{nds::ErrorCode::kInvalidArgument, app.exit(error) == 0 ? kHelpRequested : "invalid options"};
    }
    if (backend == "aiv")
        config.backend.mode = nds::client::BackendMode::Aiv;
    else if (backend == "aicpu")
        config.backend.mode = nds::client::BackendMode::Aicpu;
    if (config.backend.artifact_path.empty())
        return nds::Error{nds::ErrorCode::kInvalidArgument, "--backend-artifact-path is required"};
    if (operation == "recv")
        config.operation = Operation::Recv;
    else if (operation == "read")
        config.operation = Operation::Read;
    else if (operation == "write")
        config.operation = Operation::Write;
    return config;
}

std::byte payload_byte(std::size_t index) {
    return static_cast<std::byte>((index % kBytes) ^ 0x5aU);
}

std::vector<std::byte> expected_payload(std::size_t bytes) {
    std::vector<std::byte> value(bytes);
    for (std::size_t index = 0U; index < value.size(); ++index) value[index] = payload_byte(index);
    return value;
}

nds::Result<nds::RemoteMemory> receive_remote_memory(nds::TcpConnection *channel) {
    if (channel == nullptr)
        return nds::Error{nds::ErrorCode::kInvalidArgument, "TCP exchange channel is required"};
    nds::wire::RemoteMemory wire{};
    if (const auto received = channel->receive(std::as_writable_bytes(std::span{&wire, 1U})); !received.ok())
        return nds::Error{received.error()};
    nds::RemoteMemory decoded{};
    if (nds::transport::decode(&wire, &decoded) != nds::transport::CodecResult::Ok)
        return nds::Error{nds::ErrorCode::kTransport, "invalid remote-memory record"};
    return decoded;
}

nds::Result<void> send_control_signal(nds::TcpConnection *channel) {
    if (channel == nullptr)
        return nds::Error{nds::ErrorCode::kInvalidArgument, "TCP exchange channel is required"};
    const std::uint8_t ready{1U};
    return channel->send(std::as_bytes(std::span{&ready, 1U}));
}

nds::Result<nds::client::MemoryBuffer> prepare_payload(nds::client::Runtime *runtime, bool initialize) {
    if (runtime == nullptr)
        return nds::Error{nds::ErrorCode::kInvalidArgument, "runtime is required"};
    NDS_ASSIGN_OR_RETURN(nds::client::MemoryBuffer buffer,
                         runtime->allocate(kBytes, nds::client::MemoryLocation::Device));
    if (initialize) {
        const std::vector<std::byte> data = expected_payload(kBytes);
        NDS_RETURN_IF_ERROR(runtime->copy_to(&buffer, data.data(), data.size()));
    }
    return buffer;
}

nds::Result<void> verify_payload(nds::client::Runtime *runtime, const nds::client::MemoryBuffer &buffer) {
    if (runtime == nullptr)
        return nds::Error{nds::ErrorCode::kInvalidArgument, "runtime is required for payload verification"};
    std::vector<std::byte> observed(buffer.size());
    NDS_RETURN_IF_ERROR(runtime->copy_from(observed.data(), buffer, observed.size()));
    return observed == expected_payload(observed.size())
               ? nds::Result<void>{}
               : nds::Error{nds::ErrorCode::kRuntime, "transport payload mismatch"};
}

nds::Result<void> wait_completion(nds::client::Launcher *launcher, const nds::client::LaunchConfig &launch_config,
                                  const NdsQpDescriptor &qp, bool send_cq) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(5000U);
    while (std::chrono::steady_clock::now() < deadline) {
        NdsWc completion{};
        NDS_ASSIGN_OR_RETURN(std::uint32_t count,
                             launcher->with_config(launch_config).poll_cq(qp, send_cq, 1U, &completion));
        if (count == 0U) {
            std::this_thread::yield();
            continue;
        }
        return completion.status == NDS_WC_SUCCESS
                   ? nds::Result<void>{}
                   : nds::Error{nds::ErrorCode::kTransport, "transport completion failed"};
    }
    return nds::Error{nds::ErrorCode::kRuntime, "transport completion timed out"};
}

nds::Result<void> run(nds::client::Runtime *runtime, nds::client::Transport *transport,
                      nds::client::Launcher *launcher, const nds::client::LaunchConfig &launch_config,
                      const NdsQpDescriptor &host_qp, std::uint32_t queue_index, Operation operation) {
    const bool initialize = operation == Operation::Send || operation == Operation::Write;
    NDS_ASSIGN_OR_RETURN(nds::client::MemoryBuffer buffer, prepare_payload(runtime, initialize));
    NDS_ASSIGN_OR_RETURN(nds::client::MemoryRegion region,
                         transport->register_memory(buffer, nds::client::MemoryAccess::LocalWrite |
                                                                nds::client::MemoryAccess::RemoteWrite |
                                                                nds::client::MemoryAccess::RemoteRead));
    if (operation == Operation::Recv) {
        const NdsRecvWr receive_wr{1U, {region.address(), kBytes, region.local_key()}};
        NDS_RETURN_IF_ERROR(launcher->rdma_recv(launch_config, transport->device_transport(), queue_index, receive_wr));
        NDS_RETURN_IF_ERROR(send_control_signal(transport->exchange_channel()));
        NDS_RETURN_IF_ERROR(wait_completion(launcher, launch_config, host_qp, false));
    } else if (operation == Operation::Send) {
        const NdsSendWr send_wr{1U, NDS_WR_SEND, NDS_SEND_SIGNALED,
                                      {region.address(), kBytes, region.local_key()}, 0U, 0U, 0U};
        NDS_RETURN_IF_ERROR(launcher->rdma_send(launch_config, transport->device_transport(), queue_index, send_wr));
        NDS_RETURN_IF_ERROR(wait_completion(launcher, launch_config, host_qp, true));
    } else {
        NDS_ASSIGN_OR_RETURN(nds::RemoteMemory remote, receive_remote_memory(transport->exchange_channel()));
        const NdsSendWr data_wr{
            1U,
            operation == Operation::Read ? NDS_WR_RDMA_READ : NDS_WR_RDMA_WRITE,
            NDS_SEND_SIGNALED,
            {region.address(), kBytes, region.local_key()},
            remote.address,
            remote.remote_key,
            0U};
        if (operation == Operation::Read) {
            NDS_RETURN_IF_ERROR(launcher->rdma_read(launch_config, transport->device_transport(), queue_index, data_wr));
        } else {
            NDS_RETURN_IF_ERROR(launcher->rdma_write(launch_config, transport->device_transport(), queue_index, data_wr));
        }
        NDS_RETURN_IF_ERROR(wait_completion(launcher, launch_config, host_qp, true));
        if (operation == Operation::Write) {
            const NdsSendWr signal_wr{2U, NDS_WR_SEND, NDS_SEND_SIGNALED,
                                            {region.address(), 1U, region.local_key()}, 0U, 0U, 0U};
            NDS_RETURN_IF_ERROR(
                launcher->rdma_send(launch_config, transport->device_transport(), queue_index, signal_wr));
            NDS_RETURN_IF_ERROR(wait_completion(launcher, launch_config, host_qp, true));
        } else {
            NDS_RETURN_IF_ERROR(send_control_signal(transport->exchange_channel()));
        }
    }
    if (operation == Operation::Recv || operation == Operation::Read)
        NDS_RETURN_IF_ERROR(verify_payload(runtime, buffer));
    return {};
}
}  // namespace

int main(int argc, char **argv) {
    (void)nds::log::configure("transport-client", "stderr", "info");
    const auto config = parse(argc, argv);
    if (!config.ok()) {
        if (config.error().message == kHelpRequested)
            return EXIT_SUCCESS;
        NDS_LOG_ERROR("transport-client", "options failed: {}", config.error().message);
        return EXIT_FAILURE;
    }
    nds::client::Runtime runtime;
    nds::client::Transport transport;
    if (const auto opened = runtime.open(config.value().runtime); !opened.ok()) {
        NDS_LOG_ERROR("transport-client", "runtime open failed: {}", opened.error().message);
        return EXIT_FAILURE;
    }
    if (const auto opened = transport.open(&runtime, config.value().transport, config.value().backend); !opened.ok()) {
        NDS_LOG_ERROR("transport-client", "transport open failed: {}", opened.error().message);
        return EXIT_FAILURE;
    }
    const auto host_qp = transport.host_qp_descriptor(0U);
    if (!host_qp.ok()) {
        NDS_LOG_ERROR("transport-client", "transport QP descriptor unavailable: {}", host_qp.error().message);
        return EXIT_FAILURE;
    }
    const auto launcher = nds::client::Launcher::open(&runtime, config.value().backend.mode,
                                                      config.value().backend.artifact_path);
    if (!launcher.ok()) {
        NDS_LOG_ERROR("transport-client", "backend launcher open failed: {}", launcher.error().message);
        return EXIT_FAILURE;
    }
    aclrtStream stream{};
    if (config.value().backend.mode != nds::client::BackendMode::Ra) {
        const int result = aclrtCreateStream(&stream);
        if (result != ACL_SUCCESS || stream == nullptr) {
            NDS_LOG_ERROR("transport-client", "ACL stream creation failed: {}", result);
            return EXIT_FAILURE;
        }
    }
    const nds::client::LaunchConfig launch_config{
        .block_dim = 1U, .stream = stream, .sync = true, .sync_timeout_ms = 5000};
    if (const auto result = run(&runtime, &transport, launcher.value().get(), launch_config, host_qp.value(), 0U,
                                config.value().operation);
        !result.ok()) {
        NDS_LOG_ERROR("transport-client", "transport operation failed: {}", result.error().message);
        if (stream != nullptr)
            (void)aclrtDestroyStream(stream);
        return EXIT_FAILURE;
    }
    if (stream != nullptr)
        (void)aclrtDestroyStream(stream);
    NDS_LOG_INFO("transport-client", "completed NDS transport operation");
    return EXIT_SUCCESS;
}

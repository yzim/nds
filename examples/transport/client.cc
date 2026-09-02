#include "logging.hh"
#include "backends/launcher.hh"
#include "transport_protocol.hh"
#include "runtime.hh"
#include "transport.hh"

#include <CLI/CLI.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace {
constexpr std::size_t kBytes = 64U;
constexpr std::uint32_t kTransportStressWrCount = 65536U;
constexpr std::uint32_t kTransportReceiveWindow = 16U;
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
    app.add_option("--backend-artifact-path", config.backend.artifact_path,
                   "RA backend shared artifact, AIV kernel object, or AICPU package descriptor");
    app.add_option("--operation", operation, "Transport operation")
        ->check(CLI::IsMember({"send", "recv", "read", "write"}));
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

nds::Result<void> wait_control_signal(nds::TcpConnection *channel) {
    if (channel == nullptr)
        return nds::Error{nds::ErrorCode::kInvalidArgument, "TCP exchange channel is required"};
    std::uint8_t value{};
    if (const auto received = channel->receive(std::as_writable_bytes(std::span{&value, 1U})); !received.ok())
        return nds::Error{received.error()};
    return value == 1U ? nds::Result<void>{}
                       : nds::Error{nds::ErrorCode::kTransport, "invalid transport control signal"};
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

nds::Result<void> run_send(nds::client::Transport *transport, nds::client::Launcher *launcher,
                           std::uint32_t queue_index, const nds::client::MemoryRegion &region, aclrtStream stream) {
    for (std::uint32_t index = 0U; index < kTransportStressWrCount; ++index) {
        const NdsSendWr send_wr{
            .wr_id = index + 1U,
            .opcode = NDS_WR_SEND,
            .flags = 0U,
            .local = {.address = region.address(), .length = kBytes, .local_key = region.local_key()},
            .remote_address = 0U,
            .remote_key = 0U,
        };
        const auto submitted = launcher
                                   ->with_config({
                                       .block_dim = 1U,
                                       .stream = stream,
                                       .sync = true,
                                       .sync_timeout_ms = 5000,
                                   })
                                   .rdma_send(transport->device_transport(), queue_index, send_wr);
        if (!submitted.ok())
            return nds::Error{submitted.error().code,
                              "RdmaSend WR " + std::to_string(index + 1U) + " failed: " + submitted.error().message};
    }
    return wait_control_signal(transport->exchange_channel());
}

nds::Result<void> run_receive(nds::client::Runtime *runtime, nds::client::Transport *transport,
                              nds::client::Launcher *launcher, std::uint32_t queue_index,
                              const nds::client::MemoryRegion &region, const nds::client::MemoryBuffer &buffer,
                              aclrtStream stream) {
    for (std::uint32_t start = 0U; start < kTransportStressWrCount; start += kTransportReceiveWindow) {
        const std::uint32_t count = std::min(kTransportReceiveWindow, kTransportStressWrCount - start);
        for (std::uint32_t index = 0U; index < count; ++index) {
            const NdsRecvWr receive_wr{
                .wr_id = start + index + 1U,
                .local = {.address = region.address(), .length = kBytes, .local_key = region.local_key()},
            };
            NDS_RETURN_IF_ERROR(launcher
                                    ->with_config({
                                        .block_dim = 1U,
                                        .stream = stream,
                                        .sync = true,
                                        .sync_timeout_ms = 5000,
                                    })
                                    .rdma_recv(transport->device_transport(), queue_index, receive_wr));
        }
        // The server sends only after this TCP acknowledgement, so the receive window is armed first.
        NDS_RETURN_IF_ERROR(send_control_signal(transport->exchange_channel()));
        NDS_RETURN_IF_ERROR(wait_control_signal(transport->exchange_channel()));
    }
    return verify_payload(runtime, buffer);
}

nds::Result<void> run_read(nds::client::Runtime *runtime, nds::client::Transport *transport,
                           nds::client::Launcher *launcher, std::uint32_t queue_index,
                           const nds::client::MemoryRegion &region, const nds::client::MemoryBuffer &buffer,
                           aclrtStream stream) {
    NDS_ASSIGN_OR_RETURN(nds::RemoteMemory remote, receive_remote_memory(transport->exchange_channel()));
    for (std::uint32_t index = 0U; index < kTransportStressWrCount; ++index) {
        const NdsSendWr read_wr{
            .wr_id = index + 1U,
            .opcode = NDS_WR_RDMA_READ,
            .flags = 0U,
            .local = {.address = region.address(), .length = kBytes, .local_key = region.local_key()},
            .remote_address = remote.address,
            .remote_key = remote.remote_key,
        };
        const auto submitted = launcher
                                   ->with_config({
                                       .block_dim = 1U,
                                       .stream = stream,
                                       .sync = true,
                                       .sync_timeout_ms = 5000,
                                   })
                                   .rdma_read(transport->device_transport(), queue_index, read_wr);
        if (!submitted.ok())
            return nds::Error{submitted.error().code,
                              "RdmaRead WR " + std::to_string(index + 1U) + " failed: " + submitted.error().message};
    }
    NDS_RETURN_IF_ERROR(send_control_signal(transport->exchange_channel()));
    return verify_payload(runtime, buffer);
}

nds::Result<void> run_write(nds::client::Transport *transport, nds::client::Launcher *launcher,
                            std::uint32_t queue_index, const nds::client::MemoryRegion &region, aclrtStream stream) {
    NDS_ASSIGN_OR_RETURN(nds::RemoteMemory remote, receive_remote_memory(transport->exchange_channel()));
    for (std::uint32_t index = 0U; index < kTransportStressWrCount; ++index) {
        const NdsSendWr write_wr{
            .wr_id = index + 1U,
            .opcode = NDS_WR_RDMA_WRITE,
            .flags = 0U,
            .local = {.address = region.address(), .length = kBytes, .local_key = region.local_key()},
            .remote_address = remote.address,
            .remote_key = remote.remote_key,
        };
        const auto submitted = launcher
                                   ->with_config({
                                       .block_dim = 1U,
                                       .stream = stream,
                                       .sync = true,
                                       .sync_timeout_ms = 5000,
                                   })
                                   .rdma_write(transport->device_transport(), queue_index, write_wr);
        if (!submitted.ok())
            return nds::Error{submitted.error().code,
                              "RdmaWrite WR " + std::to_string(index + 1U) + " failed: " + submitted.error().message};
    }
    const NdsSendWr signal_wr{
        .wr_id = kTransportStressWrCount + 1U,
        .opcode = NDS_WR_SEND,
        .flags = 0U,
        .local = {.address = region.address(), .length = 1U, .local_key = region.local_key()},
        .remote_address = 0U,
        .remote_key = 0U,
    };
    return launcher
        ->with_config({
            .block_dim = 1U,
            .stream = stream,
            .sync = true,
            .sync_timeout_ms = 5000,
        })
        .rdma_send(transport->device_transport(), queue_index, signal_wr);
}

nds::Result<void> run_operation(Operation operation, nds::client::Runtime *runtime, nds::client::Transport *transport,
                                nds::client::Launcher *launcher, std::uint32_t queue_index,
                                const nds::client::MemoryRegion &region, const nds::client::MemoryBuffer &buffer,
                                aclrtStream stream) {
    switch (operation) {
        case Operation::Send:
            return run_send(transport, launcher, queue_index, region, stream);
        case Operation::Recv:
            return run_receive(runtime, transport, launcher, queue_index, region, buffer, stream);
        case Operation::Read:
            return run_read(runtime, transport, launcher, queue_index, region, buffer, stream);
        case Operation::Write:
            return run_write(transport, launcher, queue_index, region, stream);
    }
    return nds::Error{nds::ErrorCode::kInvalidArgument, "unsupported transport operation"};
}

nds::Result<void> run(int argc, char **argv) {
    NDS_ASSIGN_OR_RETURN(Config config, parse(argc, argv));

    nds::client::Runtime runtime;
    NDS_RETURN_IF_ERROR(runtime.open(config.runtime));

    nds::client::Transport transport;
    NDS_RETURN_IF_ERROR(transport.open(&runtime, config.transport, config.backend));
    NDS_ASSIGN_OR_RETURN(std::unique_ptr<nds::client::Launcher> launcher,
                         nds::client::Launcher::open(&runtime, config.backend.mode, config.backend.artifact_path));

    const bool initialize = config.operation == Operation::Send || config.operation == Operation::Write;
    NDS_ASSIGN_OR_RETURN(nds::client::MemoryBuffer buffer, prepare_payload(&runtime, initialize));
    NDS_ASSIGN_OR_RETURN(nds::client::MemoryRegion region,
                         transport.register_memory(buffer, nds::client::MemoryAccess::LocalWrite |
                                                               nds::client::MemoryAccess::RemoteWrite |
                                                               nds::client::MemoryAccess::RemoteRead));

    aclrtStream stream{};
    if (config.backend.mode != nds::client::BackendMode::Ra) {
        const int result = aclrtCreateStream(&stream);
        if (result != ACL_SUCCESS || stream == nullptr)
            return nds::Error{nds::ErrorCode::kRuntime, "aclrtCreateStream failed: " + std::to_string(result)};
    }
    struct StreamGuard {
        aclrtStream stream{};

        ~StreamGuard() {
            if (stream != nullptr)
                (void)aclrtDestroyStream(stream);
        }
    } stream_guard{stream};

    NDS_RETURN_IF_ERROR(
        run_operation(config.operation, &runtime, &transport, launcher.get(), 0U, region, buffer, stream));
    return {};
}
}  // namespace

int main(int argc, char **argv) {
    (void)nds::log::configure("transport-client", "stderr", "info");
    const nds::Result<void> run_result = run(argc, argv);
    if (!run_result.ok()) {
        if (run_result.error().message == kHelpRequested)
            return EXIT_SUCCESS;
        NDS_LOG_ERROR("transport-client", "transport example failed: {}", run_result.error().message);
        return EXIT_FAILURE;
    }
    NDS_LOG_INFO("transport-client", "completed NDS transport operation");
    return EXIT_SUCCESS;
}

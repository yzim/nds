#include "aicpu/host/launcher.hh"
#include "aiv/host/launcher.hh"
#include "nds/device_transport.h"
#include "nds/logging.hh"
#include "nds/wire/transport.hh"
#include "ra/ra.hh"
#include "runtime.hh"
#include "transport.hh"

#include <CLI/CLI.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>
#include <thread>
#include <utility>

namespace {
constexpr std::size_t kBytes = 64U;
constexpr std::int32_t kTimeoutMs = 5000;
using Payload = std::array<std::byte, kBytes>;
enum class Operation { Send, Recv, Read, Write };
enum class Entry { Poll, Send, Recv, Read, Write };

struct Config {
    nds::client::RuntimeConfig runtime;
    nds::client::TransportConfig transport;
    nds::client::ExecutionConfig execution;
    Operation operation{Operation::Send};
    bool caller_polls_cq{};
};

nds::Result<Config> parse(int argc, char **argv) {
    Config config;
    std::string backend{"ra"};
    std::string operation{"send"};
    CLI::App app{"Exercise one direct NDS transport operator."};
    app.add_option("--backend", backend)->required()->check(CLI::IsMember({"ra", "aiv", "aicpu"}));
    app.add_option("--operation", operation)->check(CLI::IsMember({"send", "recv", "read", "write"}));
    app.add_option("--ascendcl", config.runtime.ascendcl_library)->required();
    app.add_option("--runtime", config.runtime.runtime_library)->required();
    app.add_option("--ra", config.transport.endpoint.ra_library)->required();
    app.add_option("--aiv-kernel", config.execution.aiv_kernel);
    app.add_option("--aicpu-kernel-config", config.execution.aicpu_kernel_config);
    app.add_option("--logical-device", config.runtime.logical_device_id)->required();
    app.add_option("--server", config.transport.server_address)->required();
    app.add_flag("--caller-polls-cq", config.caller_polls_cq, "Request caller-owned CQ dataplane memory");
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
    if (operation == "recv")
        config.operation = Operation::Recv;
    else if (operation == "read")
        config.operation = Operation::Read;
    else if (operation == "write")
        config.operation = Operation::Write;
    if (config.caller_polls_cq)
        config.transport.qp.control_flags |= nds::client::QueuePairCallerPollsCq;
    return config;
}

Payload payload() {
    Payload value{};
    for (std::size_t index = 0U; index < value.size(); ++index) value[index] = static_cast<std::byte>(index ^ 0x5aU);
    return value;
}

nds::Result<nds::client::MemoryBuffer> allocate_return_value(nds::client::Runtime *runtime) {
    auto buffer = runtime->allocate(sizeof(std::int32_t));
    if (!buffer)
        return nds::unexpected(buffer.error());
    const std::int32_t pending = std::numeric_limits<std::int32_t>::min();
    if (const auto copied = runtime->copy_to(&*buffer, &pending, sizeof(pending)); !copied)
        return nds::unexpected(copied.error());
    return std::move(*buffer);
}

nds::Result<std::int32_t> read_return_value(nds::client::Runtime *runtime,
                                            const nds::client::MemoryBuffer &buffer) {
    std::int32_t value{};
    if (const auto copied = runtime->copy_from(&value, buffer, sizeof(value)); !copied)
        return nds::unexpected(copied.error());
    return value;
}

void set_return_value_address(void *args, Entry entry, std::uint64_t address) {
    switch (entry) {
        case Entry::Poll:
            static_cast<NdsDevicePollCqArgs *>(args)->return_value_address = address;
            return;
        case Entry::Send:
            static_cast<NdsDeviceRdmaSendArgs *>(args)->return_value_address = address;
            return;
        case Entry::Recv:
            static_cast<NdsDeviceRdmaRecvArgs *>(args)->return_value_address = address;
            return;
        case Entry::Read:
            static_cast<NdsDeviceRdmaReadArgs *>(args)->return_value_address = address;
            return;
        case Entry::Write:
            static_cast<NdsDeviceRdmaWriteArgs *>(args)->return_value_address = address;
            return;
    }
}

nds::Result<std::int32_t> launch(nds::client::Runtime *runtime, nds::client::Transport *transport, void *args,
                                 std::size_t size, Entry entry) {
    auto return_buffer = allocate_return_value(runtime);
    if (!return_buffer)
        return nds::unexpected(return_buffer.error());
    set_return_value_address(args, entry, reinterpret_cast<std::uint64_t>(return_buffer->data()));
    if (transport->execution().mode == nds::client::NpuExecutionMode::Aicpu) {
        nds::AicpuEntrypointLauncher launcher;
        if (const auto loaded = launcher.load(&runtime->acl_api(), transport->execution().aicpu_kernel_config); !loaded)
            return nds::unexpected(loaded.error());
        nds::Result<void> result;
        switch (entry) {
            case Entry::Poll:
                result = launcher.launch_poll_cq_and_wait(static_cast<NdsDevicePollCqArgs *>(args), kTimeoutMs);
                break;
            case Entry::Send:
                result = launcher.launch_rdma_send_and_wait(static_cast<NdsDeviceRdmaSendArgs *>(args), kTimeoutMs);
                break;
            case Entry::Recv:
                result = launcher.launch_rdma_recv_and_wait(static_cast<NdsDeviceRdmaRecvArgs *>(args), kTimeoutMs);
                break;
            case Entry::Read:
                result = launcher.launch_rdma_read_and_wait(static_cast<NdsDeviceRdmaReadArgs *>(args), kTimeoutMs);
                break;
            case Entry::Write:
                result = launcher.launch_rdma_write_and_wait(static_cast<NdsDeviceRdmaWriteArgs *>(args), kTimeoutMs);
                break;
        }
        if (!result)
            return nds::unexpected(result.error());
        return read_return_value(runtime, *return_buffer);
    }
    auto device_args = runtime->allocate(size);
    if (!device_args)
        return nds::unexpected(device_args.error());
    if (const auto copied = runtime->copy_to(&*device_args, args, size); !copied)
        return nds::unexpected(copied.error());
    nds::AivEntrypointLauncher launcher;
    if (const auto loaded = launcher.load(&runtime->acl_api(), transport->execution().aiv_kernel); !loaded)
        return nds::unexpected(loaded.error());
    const std::uint64_t address = reinterpret_cast<std::uint64_t>(device_args->data());
    nds::Result<void> result;
    switch (entry) {
        case Entry::Poll:
            result = launcher.launch_poll_cq_and_wait(address, kTimeoutMs);
            break;
        case Entry::Send:
            result = launcher.launch_rdma_send_and_wait(address, kTimeoutMs);
            break;
        case Entry::Recv:
            result = launcher.launch_rdma_recv_and_wait(address, kTimeoutMs);
            break;
        case Entry::Read:
            result = launcher.launch_rdma_read_and_wait(address, kTimeoutMs);
            break;
        case Entry::Write:
            result = launcher.launch_rdma_write_and_wait(address, kTimeoutMs);
            break;
    }
    if (!result)
        return nds::unexpected(result.error());
    if (const auto copied = runtime->copy_from(args, *device_args, size); !copied)
        return nds::unexpected(copied.error());
    return read_return_value(runtime, *return_buffer);
}

nds::Result<void> poll(nds::client::Runtime *runtime, nds::client::Transport *transport, bool send_cq) {
    if (transport->execution().mode == nds::client::NpuExecutionMode::Ra) {
        NdsDeviceWc completion{};
        for (std::uint32_t elapsed = 0U; elapsed < kTimeoutMs; elapsed += 10U) {
            const auto result = nds::NdsRaPollCq(transport->qp(), send_cq, 1U, &completion);
            if (!result)
                return nds::unexpected(result.error());
            if (*result != 0U)
                return {};
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return nds::unexpected(nds::ErrorCode::kRuntime, "timed out polling RA CQ");
    }
    auto wcs = runtime->allocate(sizeof(NdsDeviceWc));
    if (!wcs)
        return nds::unexpected(wcs.error());
    const auto device = transport->qp()->make_device_transport();
    if (!device)
        return nds::unexpected(device.error());
    for (std::uint32_t elapsed = 0U; elapsed < kTimeoutMs; elapsed += 10U) {
        NdsDevicePollCqArgs args{device->control_qp, send_cq ? 1U : 0U, 1U,
                                 reinterpret_cast<std::uint64_t>(wcs->data()), 0U};
        const auto result = launch(runtime, transport, &args, sizeof(args), Entry::Poll);
        if (!result)
            return nds::unexpected(result.error());
        if (*result > 0)
            return {};
        if (*result < 0)
            return nds::unexpected(nds::ErrorCode::kRuntime, "device PollCq failed: " + std::to_string(*result));
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return nds::unexpected(nds::ErrorCode::kRuntime, "timed out polling device CQ");
}

nds::Result<void> send(nds::client::Runtime *runtime, nds::client::Transport *transport, const NdsDeviceSendWr &wr,
                       Entry entry) {
    if (transport->execution().mode == nds::client::NpuExecutionMode::Ra) {
        const nds::RaConnection connection{runtime, transport->qp()};
        if (entry == Entry::Send)
            return nds::NdsRaRdmaSend(connection, wr);
        if (entry == Entry::Read)
            return nds::NdsRaRdmaRead(connection, wr);
        return nds::NdsRaRdmaWrite(connection, wr);
    }
    const auto device = transport->qp()->make_device_transport();
    if (!device)
        return nds::unexpected(device.error());
    if (entry == Entry::Send) {
        NdsDeviceRdmaSendArgs args{*device, wr, 0U};
        const auto result = launch(runtime, transport, &args, sizeof(args), entry);
        if (!result)
            return nds::unexpected(result.error());
        return *result == 0 ? nds::Result<void>{}
                            : nds::unexpected(nds::ErrorCode::kRuntime,
                                              "RdmaSend failed: " + std::to_string(*result));
    }
    if (entry == Entry::Read) {
        NdsDeviceRdmaReadArgs args{*device, wr, 0U};
        const auto result = launch(runtime, transport, &args, sizeof(args), entry);
        if (!result)
            return nds::unexpected(result.error());
        return *result == 0 ? nds::Result<void>{}
                            : nds::unexpected(nds::ErrorCode::kRuntime,
                                              "RdmaRead failed: " + std::to_string(*result));
    }
    NdsDeviceRdmaWriteArgs args{*device, wr, 0U};
    const auto result = launch(runtime, transport, &args, sizeof(args), entry);
    if (!result)
        return nds::unexpected(result.error());
    return *result == 0 ? nds::Result<void>{}
                        : nds::unexpected(nds::ErrorCode::kRuntime,
                                          "RdmaWrite failed: " + std::to_string(*result));
}

nds::Result<void> receive(nds::client::Runtime *runtime, nds::client::Transport *transport, const NdsDeviceRecvWr &wr) {
    if (transport->execution().mode == nds::client::NpuExecutionMode::Ra)
        return nds::NdsRaRdmaRecv({runtime, transport->qp()}, {wr.wr_id, NDS_DEVICE_WR_SEND, 0U, wr.local, 0U, 0U, 0U});
    const auto device = transport->qp()->make_device_transport();
    if (!device)
        return nds::unexpected(device.error());
    NdsDeviceRdmaRecvArgs args{*device, wr, 0U};
    const auto result = launch(runtime, transport, &args, sizeof(args), Entry::Recv);
    if (!result)
        return nds::unexpected(result.error());
    return *result == 0 ? nds::Result<void>{}
                        : nds::unexpected(nds::ErrorCode::kRuntime,
                                          "RdmaRecv failed: " + std::to_string(*result));
}

nds::Result<void> signal(nds::client::Transport *transport) {
    const std::uint8_t value = 1U;
    return transport->bootstrap()->send_bytes(&value, sizeof(value));
}

nds::Result<nds::transport::RemoteMemory> remote_memory(nds::client::Transport *transport) {
    nds::wire::RemoteMemory wire{};
    if (const auto received = transport->bootstrap()->receive_bytes(&wire, sizeof(wire)); !received)
        return nds::unexpected(received.error());
    nds::transport::RemoteMemory memory{};
    if (nds::transport::decode(&wire, &memory) != nds::transport::CodecResult::Ok)
        return nds::unexpected(nds::ErrorCode::kTransport, "invalid remote-memory record");
    return memory;
}

nds::Result<void> verify(nds::client::Runtime *runtime, const nds::client::MemoryBuffer &buffer) {
    Payload observed{};
    if (const auto copied = runtime->copy_from(observed.data(), buffer, observed.size()); !copied)
        return nds::unexpected(copied.error());
    return observed == payload() ? nds::Result<void>{} : nds::unexpected(nds::ErrorCode::kRuntime, "payload mismatch");
}

nds::Result<void> run(nds::client::Runtime *runtime, nds::client::Transport *transport, Operation operation) {
    auto allocated = runtime->allocate(kBytes);
    if (!allocated)
        return nds::unexpected(allocated.error());
    nds::client::MemoryBuffer buffer = std::move(*allocated);
    if (operation == Operation::Send || operation == Operation::Write) {
        const Payload data = payload();
        if (const auto copied = runtime->copy_to(&buffer, data.data(), data.size()); !copied)
            return nds::unexpected(copied.error());
    }
    auto registered = transport->endpoint()->reg_mr(buffer, nds::client::MemoryAccess::DirectNpu);
    if (!registered)
        return nds::unexpected(registered.error());
    nds::client::MemoryRegion region = std::move(*registered);
    if (operation == Operation::Recv) {
        if (const auto result = receive(runtime, transport, {1U, {region.address(), kBytes, region.local_key()}});
            !result)
            return nds::unexpected(result.error());
        if (const auto result = signal(transport); !result)
            return nds::unexpected(result.error());
        if (transport->execution().mode == nds::client::NpuExecutionMode::Aiv) {
            if (const auto result = poll(runtime, transport, false); !result)
                return nds::unexpected(result.error());
        }
        return verify(runtime, buffer);
    }
    Entry entry = Entry::Send;
    NdsDeviceSendWr wr{
        1U, NDS_DEVICE_WR_SEND, NDS_DEVICE_SEND_SIGNALED, {region.address(), kBytes, region.local_key()}, 0U, 0U, 0U};
    if (operation == Operation::Read || operation == Operation::Write) {
        const auto memory = remote_memory(transport);
        if (!memory)
            return nds::unexpected(memory.error());
        entry = operation == Operation::Read ? Entry::Read : Entry::Write;
        wr.opcode = operation == Operation::Read ? NDS_DEVICE_WR_RDMA_READ : NDS_DEVICE_WR_RDMA_WRITE;
        wr.remote_address = memory->address;
        wr.remote_key = memory->remote_key;
    }
    if (const auto result = send(runtime, transport, wr, entry); !result)
        return nds::unexpected(result.error());
    if (transport->execution().mode == nds::client::NpuExecutionMode::Aiv) {
        if (const auto result = poll(runtime, transport, true); !result)
            return nds::unexpected(result.error());
    }
    if (operation == Operation::Read) {
        if (const auto result = verify(runtime, buffer); !result)
            return nds::unexpected(result.error());
    }
    if (operation == Operation::Write) {
        const NdsDeviceSendWr completion{2U,
                                         NDS_DEVICE_WR_SEND,
                                         NDS_DEVICE_SEND_SIGNALED,
                                         {region.address(), 1U, region.local_key()},
                                         0U,
                                         0U,
                                         0U};
        return send(runtime, transport, completion, Entry::Send);
    }
    if (operation == Operation::Read)
        return signal(transport);
    return {};
}
}  // namespace

int main(int argc, char **argv) {
    (void)nds::log::configure("transport-client", "stderr", "info");
    const auto config = parse(argc, argv);
    if (!config) {
        NDS_LOG_ERROR("transport-client", "options failed: {}", config.error().message);
        return EXIT_FAILURE;
    }
    nds::client::Runtime runtime;
    nds::client::Transport transport;
    if (const auto opened = runtime.open(config->runtime); !opened) {
        NDS_LOG_ERROR("transport-client", "runtime open failed: {}", opened.error().message);
        return EXIT_FAILURE;
    }
    if (const auto opened = transport.open(&runtime, config->transport, config->execution); !opened) {
        NDS_LOG_ERROR("transport-client", "transport open failed: {}", opened.error().message);
        return EXIT_FAILURE;
    }
    if (const auto result = run(&runtime, &transport, config->operation); !result) {
        NDS_LOG_ERROR("transport-client", "transport operation failed: {}", result.error().message);
        return EXIT_FAILURE;
    }
    NDS_LOG_INFO("transport-client", "completed NDS transport operation");
    return EXIT_SUCCESS;
}

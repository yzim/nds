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

struct AivThreeAddressArguments {
    std::uint64_t first_address;
    std::uint64_t second_address;
    std::uint64_t return_value_address;
};

struct AivPollCqArguments {
    std::uint64_t qp_address;
    std::uint32_t is_send_cq;
    std::uint32_t max_completions;
    std::uint64_t wc_address;
    std::uint64_t return_value_address;
};

struct Config {
    nds::client::RuntimeConfig runtime;
    nds::client::TransportConfig transport;
    nds::client::BackendConfig backend;
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
    app.add_option("--cann-runtime", config.runtime.cann_runtime_library)->required();
    app.add_option("--ra", config.transport.endpoint.ra_library)->required();
    app.add_option("--aiv-kernel", config.backend.aiv_kernel);
    app.add_option("--aicpu-kernel-config", config.backend.aicpu_kernel_config);
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
        config.backend.mode = nds::client::NpuBackend::Aiv;
    if (backend == "aicpu")
        config.backend.mode = nds::client::NpuBackend::Aicpu;
    if ((config.backend.mode == nds::client::NpuBackend::Aiv && config.backend.aiv_kernel.empty()) ||
        (config.backend.mode == nds::client::NpuBackend::Aicpu &&
         config.backend.aicpu_kernel_config.empty())) {
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

void set_return_value(void *args, Entry entry, std::int32_t value) {
    switch (entry) {
        case Entry::Poll:
            static_cast<NdsDevicePollCqArgs *>(args)->return_value = value;
            return;
        case Entry::Send:
            static_cast<NdsDeviceRdmaSendArgs *>(args)->return_value = value;
            return;
        case Entry::Recv:
            static_cast<NdsDeviceRdmaRecvArgs *>(args)->return_value = value;
            return;
        case Entry::Read:
            static_cast<NdsDeviceRdmaReadArgs *>(args)->return_value = value;
            return;
        case Entry::Write:
            static_cast<NdsDeviceRdmaWriteArgs *>(args)->return_value = value;
            return;
    }
}

std::int32_t return_value(const void *args, Entry entry) {
    switch (entry) {
        case Entry::Poll:
            return static_cast<const NdsDevicePollCqArgs *>(args)->return_value;
        case Entry::Send:
            return static_cast<const NdsDeviceRdmaSendArgs *>(args)->return_value;
        case Entry::Recv:
            return static_cast<const NdsDeviceRdmaRecvArgs *>(args)->return_value;
        case Entry::Read:
            return static_cast<const NdsDeviceRdmaReadArgs *>(args)->return_value;
        case Entry::Write:
            return static_cast<const NdsDeviceRdmaWriteArgs *>(args)->return_value;
    }
    return std::numeric_limits<std::int32_t>::min();
}

nds::Result<std::int32_t> launch(nds::client::Runtime *runtime, nds::client::Transport *transport, void *args,
                                 std::size_t size, Entry entry) {
    set_return_value(args, entry, std::numeric_limits<std::int32_t>::min());
    auto device_args = runtime->allocate(size);
    if (!device_args)
        return nds::unexpected(device_args.error());
    if (const auto copied = runtime->copy_to(&*device_args, args, size); !copied)
        return nds::unexpected(copied.error());
    const std::uint64_t address = reinterpret_cast<std::uint64_t>(device_args->data());
    if (transport->backend().mode == nds::client::NpuBackend::Aicpu) {
        nds::AicpuLauncher launcher;
        if (const auto loaded = launcher.load(transport->backend().aicpu_kernel_config); !loaded)
            return nds::unexpected(loaded.error());
        const char *kernel_name = nullptr;
        switch (entry) {
            case Entry::Poll:
                kernel_name = "nds_aicpu_poll_cq_kernel";
                break;
            case Entry::Send:
                kernel_name = "nds_aicpu_rdma_send_kernel";
                break;
            case Entry::Recv:
                kernel_name = "nds_aicpu_rdma_recv_kernel";
                break;
            case Entry::Read:
                kernel_name = "nds_aicpu_rdma_read_kernel";
                break;
            case Entry::Write:
                kernel_name = "nds_aicpu_rdma_write_kernel";
                break;
        }
        const nds::Result<void> result = launcher.launch_and_wait(kernel_name, address, kTimeoutMs);
        if (!result)
            return nds::unexpected(result.error());
    } else {
        nds::AivLauncher launcher;
        if (const auto loaded = launcher.load(transport->backend().aiv_kernel); !loaded)
            return nds::unexpected(loaded.error());
        const char *kernel_name = nullptr;
        switch (entry) {
            case Entry::Poll: {
                const auto *poll = static_cast<const NdsDevicePollCqArgs *>(args);
                AivPollCqArguments arguments{address + offsetof(NdsDevicePollCqArgs, qp), poll->is_send_cq,
                                              poll->max_completions, poll->wc_address,
                                              address + offsetof(NdsDevicePollCqArgs, return_value)};
                const auto result =
                    launcher.launch_and_wait("nds_aiv_poll_cq_kernel", &arguments, sizeof(arguments), kTimeoutMs);
                if (!result)
                    return nds::unexpected(result.error());
                break;
            }
            case Entry::Send:
                kernel_name = "nds_aiv_rdma_send_kernel";
                break;
            case Entry::Recv:
                kernel_name = "nds_aiv_rdma_recv_kernel";
                break;
            case Entry::Read:
                kernel_name = "nds_aiv_rdma_read_kernel";
                break;
            case Entry::Write:
                kernel_name = "nds_aiv_rdma_write_kernel";
                break;
        }
        if (entry != Entry::Poll) {
            std::uint64_t transport_address = 0U;
            std::uint64_t wr_address = 0U;
            std::uint64_t return_value_address = 0U;
            switch (entry) {
                case Entry::Send:
                    transport_address = address + offsetof(NdsDeviceRdmaSendArgs, transport);
                    wr_address = address + offsetof(NdsDeviceRdmaSendArgs, wr);
                    return_value_address = address + offsetof(NdsDeviceRdmaSendArgs, return_value);
                    break;
                case Entry::Recv:
                    transport_address = address + offsetof(NdsDeviceRdmaRecvArgs, transport);
                    wr_address = address + offsetof(NdsDeviceRdmaRecvArgs, wr);
                    return_value_address = address + offsetof(NdsDeviceRdmaRecvArgs, return_value);
                    break;
                case Entry::Read:
                    transport_address = address + offsetof(NdsDeviceRdmaReadArgs, transport);
                    wr_address = address + offsetof(NdsDeviceRdmaReadArgs, wr);
                    return_value_address = address + offsetof(NdsDeviceRdmaReadArgs, return_value);
                    break;
                case Entry::Write:
                    transport_address = address + offsetof(NdsDeviceRdmaWriteArgs, transport);
                    wr_address = address + offsetof(NdsDeviceRdmaWriteArgs, wr);
                    return_value_address = address + offsetof(NdsDeviceRdmaWriteArgs, return_value);
                    break;
                case Entry::Poll:
                    break;
            }
            AivThreeAddressArguments arguments{transport_address, wr_address, return_value_address};
            const auto result = launcher.launch_and_wait(kernel_name, &arguments, sizeof(arguments), kTimeoutMs);
            if (!result)
                return nds::unexpected(result.error());
        }
    }
    if (const auto copied = runtime->copy_from(args, *device_args, size); !copied)
        return nds::unexpected(copied.error());
    return return_value(args, entry);
}

nds::Result<void> poll(nds::client::Runtime *runtime, nds::client::Transport *transport, bool send_cq) {
    if (transport->backend().mode == nds::client::NpuBackend::Ra) {
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
    if (transport->backend().mode == nds::client::NpuBackend::Ra) {
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
                            : nds::unexpected(nds::ErrorCode::kRuntime, "RdmaSend failed: " + std::to_string(*result));
    }
    if (entry == Entry::Read) {
        NdsDeviceRdmaReadArgs args{*device, wr, 0U};
        const auto result = launch(runtime, transport, &args, sizeof(args), entry);
        if (!result)
            return nds::unexpected(result.error());
        return *result == 0 ? nds::Result<void>{}
                            : nds::unexpected(nds::ErrorCode::kRuntime, "RdmaRead failed: " + std::to_string(*result));
    }
    NdsDeviceRdmaWriteArgs args{*device, wr, 0U};
    const auto result = launch(runtime, transport, &args, sizeof(args), entry);
    if (!result)
        return nds::unexpected(result.error());
    return *result == 0 ? nds::Result<void>{}
                        : nds::unexpected(nds::ErrorCode::kRuntime, "RdmaWrite failed: " + std::to_string(*result));
}

nds::Result<void> receive(nds::client::Runtime *runtime, nds::client::Transport *transport, const NdsDeviceRecvWr &wr) {
    if (transport->backend().mode == nds::client::NpuBackend::Ra)
        return nds::NdsRaRdmaRecv({runtime, transport->qp()}, {wr.wr_id, NDS_DEVICE_WR_SEND, 0U, wr.local, 0U, 0U, 0U});
    const auto device = transport->qp()->make_device_transport();
    if (!device)
        return nds::unexpected(device.error());
    NdsDeviceRdmaRecvArgs args{*device, wr, 0U};
    const auto result = launch(runtime, transport, &args, sizeof(args), Entry::Recv);
    if (!result)
        return nds::unexpected(result.error());
    return *result == 0 ? nds::Result<void>{}
                        : nds::unexpected(nds::ErrorCode::kRuntime, "RdmaRecv failed: " + std::to_string(*result));
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
        if (transport->backend().mode == nds::client::NpuBackend::Aiv) {
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
    if (transport->backend().mode == nds::client::NpuBackend::Aiv) {
        if (const auto result = poll(runtime, transport, true); !result)
            return nds::unexpected(result.error());
    }
    if (operation == Operation::Read) {
        if (const auto result = verify(runtime, buffer); !result)
            return nds::unexpected(result.error());
    }
    if (operation == Operation::Write) {
        const NdsDeviceSendWr completion{
            2U, NDS_DEVICE_WR_SEND, NDS_DEVICE_SEND_SIGNALED, {region.address(), 1U, region.local_key()}, 0U, 0U, 0U};
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
    if (const auto opened = transport.open(&runtime, config->transport, config->backend); !opened) {
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

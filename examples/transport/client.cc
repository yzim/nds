#include "logging.hh"
#include "transport_protocol.hh"
#include "runtime.hh"
#include "transport.hh"

#include <CLI/CLI.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <string>

namespace {
constexpr std::size_t kBytes = 64U;
using Payload = std::array<std::byte, kBytes>;
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
    CLI::App app{"Exercise NDS transport requests."};
    app.add_option("--backend-mode", backend)->required()->check(CLI::IsMember({"ra", "aiv", "aicpu"}));
    app.add_option("--operation", operation)->check(CLI::IsMember({"send", "recv", "read", "write"}));
    app.add_option("--cann-runtime", config.runtime.cann_runtime_library)->required();
    app.add_option("--ra", config.transport.endpoint.ra_library)->required();
    app.add_option("--backend-artifact", config.backend.artifact);
    app.add_option("--logical-device", config.runtime.logical_device_id)->required();
    app.add_option("--server", config.transport.server_address)->required();
    app.add_option("--qp-count", config.transport.qp_count)->check(CLI::Range(1U, nds::wire::kMaxQpInfoBatch));
    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError &error) {
        return Error{nds::ErrorCode::kInvalidArgument, app.exit(error) == 0 ? "help requested" : "invalid options"};
    }
    if (backend == "aiv")
        config.backend.mode = nds::client::BackendMode::Aiv;
    else if (backend == "aicpu")
        config.backend.mode = nds::client::BackendMode::Aicpu;
    if (config.backend.artifact.empty())
        return Error{nds::ErrorCode::kInvalidArgument, "--backend-artifact is required"};
    if (operation == "recv")
        config.operation = Operation::Recv;
    else if (operation == "read")
        config.operation = Operation::Read;
    else if (operation == "write")
        config.operation = Operation::Write;
    return config;
}

Payload payload() {
    Payload value{};
    for (std::size_t index = 0U; index < value.size(); ++index) value[index] = static_cast<std::byte>(index ^ 0x5aU);
    return value;
}

nds::Result<nds::client::RemoteMemory> remote_memory(nds::client::Transport *transport) {
    nds::wire::RemoteMemory wire{};
    if (const auto received = transport->exchange_channel()->receive(std::as_writable_bytes(std::span{&wire, 1U}));
        !received)
        return Error{received.error()};
    nds::transport::RemoteMemory decoded{};
    if (nds::transport::decode(&wire, &decoded) != nds::transport::CodecResult::Ok)
        return Error{nds::ErrorCode::kTransport, "invalid remote-memory record"};
    return {{decoded.address, decoded.remote_key, decoded.length}};
}

nds::Result<void> run(nds::client::Runtime *runtime, nds::client::Transport *transport, nds::client::QueueHandle queue,
                      Operation operation) {
    auto buffer = runtime->allocate(kBytes);
    if (!buffer)
        return Error{buffer.error()};
    if (operation == Operation::Send || operation == Operation::Write) {
        const Payload data = payload();
        if (const auto copied = runtime->copy_to(&*buffer, data.data(), data.size()); !copied)
            return Error{copied.error()};
    }
    const auto region = transport->register_memory(*buffer, nds::client::MemoryAccess::DirectNpu);
    if (!region)
        return Error{region.error()};
    if (operation == Operation::Recv) {
        if (const auto posted = transport->receive(queue, {&*region, kBytes}); !posted)
            return Error{posted.error()};
        const std::uint8_t ready{1U};
        if (const auto sent = transport->exchange_channel()->send(std::as_bytes(std::span{&ready, 1U})); !sent)
            return Error{sent.error()};
        if (const auto completed = transport->wait_receive(queue); !completed)
            return Error{completed.error()};
    } else if (operation == Operation::Send) {
        if (const auto sent = transport->send(queue, {&*region, kBytes}); !sent)
            return Error{sent.error()};
    } else {
        const auto remote = remote_memory(transport);
        if (!remote)
            return Error{remote.error()};
        const auto moved = operation == Operation::Read ? transport->read(queue, {&*region, *remote, kBytes})
                                                        : transport->write(queue, {&*region, *remote, kBytes});
        if (!moved)
            return Error{moved.error()};
        if (operation == Operation::Write) {
            if (const auto sent = transport->send(queue, {&*region, 1U}); !sent)
                return Error{sent.error()};
        } else {
            const std::uint8_t ready{1U};
            if (const auto sent = transport->exchange_channel()->send(std::as_bytes(std::span{&ready, 1U})); !sent)
                return Error{sent.error()};
        }
    }
    if (operation == Operation::Recv || operation == Operation::Read) {
        Payload observed{};
        if (const auto copied = runtime->copy_from(observed.data(), *buffer, observed.size()); !copied)
            return Error{copied.error()};
        if (observed != payload())
            return Error{nds::ErrorCode::kRuntime, "payload mismatch"};
    }
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
    const auto queue = transport.queue(0U);
    if (!queue) {
        NDS_LOG_ERROR("transport-client", "transport queue unavailable: {}", queue.error().message);
        return EXIT_FAILURE;
    }
    if (const auto result = run(&runtime, &transport, *queue, config->operation); !result) {
        NDS_LOG_ERROR("transport-client", "transport operation failed: {}", result.error().message);
        return EXIT_FAILURE;
    }
    NDS_LOG_INFO("transport-client", "completed NDS transport operation");
    return EXIT_SUCCESS;
}

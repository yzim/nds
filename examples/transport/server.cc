#include "logging.hh"
#include "transport_protocol.hh"
#include "transport.hh"

#include <CLI/CLI.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
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
    nds::server::TransportConfig transport;
    Operation operation{Operation::Send};
    std::string log_sink{"stderr"};
    std::string log_level{"info"};
};

nds::Result<Config> parse(int argc, char **argv) {
    Config config;
    std::string operation{"send"};
    CLI::App app{"Peer one NDS indexed transport operation over a CPU RNIC."};
    app.add_option("--device", config.transport.endpoint.device_name, "CPU RNIC device name")->required();
    app.add_option("--gid-index", config.transport.endpoint.gid_index, "CPU RNIC GID index")->required();
    app.add_option("--listen", config.transport.listen_address, "TCP listen address as IPv4:port");
    app.add_option("--ib-port", config.transport.endpoint.port, "CPU RNIC port")
        ->check(CLI::Range(std::uint8_t{1U}, std::numeric_limits<std::uint8_t>::max()));
    app.add_option("--max-qp-count", config.transport.max_qp_count, "Maximum QPs accepted per client")
        ->check(CLI::Range(1U, nds::wire::kMaxQpInfoBatch));
    app.add_option("--operation", operation, "Transport operation")
        ->check(CLI::IsMember({"send", "recv", "read", "write"}));
    app.add_option("--log-sink", config.log_sink, "Log sink")
        ->check(CLI::IsMember({"stderr", "stdout", "syslog", "none"}));
    app.add_option("--log-level", config.log_level, "Log level")
        ->check(CLI::IsMember({"trace", "debug", "info", "warn", "error", "critical", "off"}));
    try {
        app.parse(argc, argv);
    } catch (const CLI::CallForHelp &help) {
        return nds::Error{nds::ErrorCode::kInvalidArgument, app.exit(help) == 0 ? kHelpRequested : "invalid options"};
    } catch (const CLI::ParseError &error) {
        return nds::Error{nds::ErrorCode::kInvalidArgument, app.exit(error) == 0 ? kHelpRequested : "invalid options"};
    }
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

nds::Result<void> wait_control_signal(nds::TcpConnection *channel) {
    if (channel == nullptr)
        return nds::Error{nds::ErrorCode::kInvalidArgument, "TCP exchange channel is required"};
    std::uint8_t value{};
    if (const auto received = channel->receive(std::as_writable_bytes(std::span{&value, 1U})); !received.ok())
        return nds::Error{received.error()};
    if (value != 1U)
        return nds::Error{nds::ErrorCode::kTransport, "invalid transport control signal"};
    return {};
}

nds::Result<void> send_control_signal(nds::TcpConnection *channel) {
    if (channel == nullptr)
        return nds::Error{nds::ErrorCode::kInvalidArgument, "TCP exchange channel is required"};
    const std::uint8_t value{1U};
    return channel->send(std::as_bytes(std::span{&value, 1U}));
}

nds::Result<void> publish_memory(nds::TcpConnection *channel, const nds::server::MemoryRegion &region) {
    if (channel == nullptr || region.address() == nullptr || region.length() == 0U ||
        region.length() > std::numeric_limits<std::uint32_t>::max() || region.remote_key() == 0U)
        return nds::Error{nds::ErrorCode::kInvalidArgument, "cannot publish an invalid registered memory region"};
    nds::wire::RemoteMemory wire{};
    const nds::RemoteMemory memory{reinterpret_cast<std::uint64_t>(region.address()),
                                   static_cast<std::uint32_t>(region.length()), region.remote_key()};
    if (nds::transport::encode(&memory, &wire) != nds::transport::CodecResult::Ok)
        return nds::Error{nds::ErrorCode::kTransport, "cannot encode published memory"};
    return channel->send(std::as_bytes(std::span{&wire, 1U}));
}

bool verify(const std::vector<std::byte> &received) {
    return received == expected_payload(received.size());
}

nds::Result<void> run_send(nds::server::Transport *transport, std::uint32_t completion_timeout_ms) {
    std::vector<std::byte> buffer(kBytes);
    NDS_ASSIGN_OR_RETURN(
        nds::server::MemoryRegion receive_region,
        transport->register_memory(buffer.data(), buffer.size(), nds::server::MemoryAccess::LocalWrite));
    for (std::uint32_t index = 0U; index < kTransportStressWrCount; ++index) {
        NDS_RETURN_IF_ERROR(transport->post_receive(receive_region));
        NDS_RETURN_IF_ERROR(transport->wait_receive(completion_timeout_ms));
        if (!verify(buffer))
            return nds::Error{nds::ErrorCode::kRuntime, "RdmaSend payload mismatch"};
    }
    return send_control_signal(transport->exchange_channel());
}

nds::Result<void> run_receive(nds::server::Transport *transport) {
    std::vector<std::byte> buffer = expected_payload(kBytes);
    NDS_ASSIGN_OR_RETURN(
        nds::server::MemoryRegion region,
        transport->register_memory(buffer.data(), buffer.size(), nds::server::MemoryAccess::LocalRead));
    for (std::uint32_t start = 0U; start < kTransportStressWrCount; start += kTransportReceiveWindow) {
        const std::uint32_t count = std::min(kTransportReceiveWindow, kTransportStressWrCount - start);
        NDS_RETURN_IF_ERROR(wait_control_signal(transport->exchange_channel()));
        for (std::uint32_t index = 0U; index < count; ++index)
            NDS_RETURN_IF_ERROR(transport->send(region, static_cast<std::uint32_t>(buffer.size())));
        NDS_RETURN_IF_ERROR(send_control_signal(transport->exchange_channel()));
    }
    return {};
}

nds::Result<void> run_read(nds::server::Transport *transport) {
    std::vector<std::byte> buffer = expected_payload(kBytes);
    NDS_ASSIGN_OR_RETURN(
        nds::server::MemoryRegion region,
        transport->register_memory(buffer.data(), buffer.size(), nds::server::MemoryAccess::RemoteRead));
    NDS_RETURN_IF_ERROR(publish_memory(transport->exchange_channel(), region));
    NDS_RETURN_IF_ERROR(wait_control_signal(transport->exchange_channel()));
    return {};
}

nds::Result<void> run_write(nds::server::Transport *transport, std::uint32_t completion_timeout_ms) {
    std::array<std::byte, 1U> completion{};
    // This receive is the client's post-write control message.
    NDS_ASSIGN_OR_RETURN(
        nds::server::MemoryRegion control_region,
        transport->register_memory(completion.data(), completion.size(), nds::server::MemoryAccess::LocalWrite));
    NDS_RETURN_IF_ERROR(transport->post_receive(control_region));
    std::vector<std::byte> buffer(kBytes);
    NDS_ASSIGN_OR_RETURN(
        nds::server::MemoryRegion region,
        transport->register_memory(buffer.data(), buffer.size(), nds::server::MemoryAccess::RemoteWrite));
    NDS_RETURN_IF_ERROR(publish_memory(transport->exchange_channel(), region));
    NDS_RETURN_IF_ERROR(transport->wait_receive(completion_timeout_ms));
    if (!verify(buffer))
        return nds::Error{nds::ErrorCode::kRuntime, "RdmaWrite payload mismatch"};
    return {};
}

nds::Result<void> run(nds::server::Transport *transport, Operation operation, std::uint32_t completion_timeout_ms) {
    if (transport == nullptr)
        return nds::Error{nds::ErrorCode::kInvalidArgument, "transport is required"};
    switch (operation) {
        case Operation::Send:
            return run_send(transport, completion_timeout_ms);
        case Operation::Recv:
            return run_receive(transport);
        case Operation::Read:
            return run_read(transport);
        case Operation::Write:
            return run_write(transport, completion_timeout_ms);
    }
    return nds::Error{nds::ErrorCode::kInvalidArgument, "unsupported transport operation"};
}

}  // namespace

int main(int argc, char **argv) {
    (void)nds::log::configure("transport-server", "stderr", "info");
    const auto config = parse(argc, argv);
    if (!config.ok()) {
        if (config.error().message == kHelpRequested)
            return EXIT_SUCCESS;
        NDS_LOG_ERROR("transport-server", "options failed: {}", config.error().message);
        return EXIT_FAILURE;
    }
    if (const auto configured =
            nds::log::configure("transport-server", config.value().log_sink, config.value().log_level);
        !configured.ok()) {
        NDS_LOG_ERROR("transport-server", "invalid logger configuration: {}", configured.error().message);
        return EXIT_FAILURE;
    }
    nds::server::TransportListener listener;
    if (const auto opened = listener.open(config.value().transport); !opened.ok()) {
        NDS_LOG_ERROR("transport-server", "listener open failed: {}", opened.error().message);
        return EXIT_FAILURE;
    }
    nds::server::Transport transport;
    if (const auto accepted = listener.accept(&transport); !accepted.ok()) {
        NDS_LOG_ERROR("transport-server", "transport accept failed: {}", accepted.error().message);
        return EXIT_FAILURE;
    }
    if (const auto result = run(&transport, config.value().operation, config.value().transport.completion_timeout_ms);
        !result.ok()) {
        NDS_LOG_ERROR("transport-server", "transport operation failed: {}", result.error().message);
        return EXIT_FAILURE;
    }
    NDS_LOG_INFO("transport-server", "completed NDS transport operation");
    return EXIT_SUCCESS;
}

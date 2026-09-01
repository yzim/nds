#include "wire.hh"
#include "endpoint.hh"
#include "logging.hh"

#include <CLI/CLI.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <string>
#include <thread>

namespace {

struct Config {
    nds::server::EndpointConfig endpoint;
    std::string listen{"0.0.0.0:18515"};
};

bool payload_matches(const std::array<std::byte, 64U> &payload, std::byte expected) {
    return std::all_of(payload.begin(), payload.end(), [expected](std::byte value) { return value == expected; });
}

bool wait_for_payload(const std::array<std::byte, 64U> &payload, std::byte expected, std::uint32_t timeout_ms) {
    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (payload_matches(payload, expected))
            return true;
        std::this_thread::yield();
    }
    return payload_matches(payload, expected);
}

nds::Result<Config> parse(int argc, char **argv) {
    Config config;
    CLI::App app{"Receive a direct RA verbs Send with a TCP QP bootstrap."};
    app.add_option("--device", config.endpoint.device_name)->required();
    app.add_option("--gid-index", config.endpoint.gid_index)->required();
    app.add_option("--listen", config.listen);
    app.add_option("--ib-port", config.endpoint.port);
    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError &error) {
        return nds::Error{nds::ErrorCode::kInvalidArgument,
                          app.exit(error) == 0 ? "help requested" : "invalid options"};
    }
    return config;
}

}  // namespace

int main(int argc, char **argv) {
    (void)nds::log::configure("verbs-server", "stderr", "info");
    const nds::Result<Config> config_result = parse(argc, argv);
    if (!config_result.ok()) {
        NDS_LOG_ERROR("verbs-server", "options failed: {}", config_result.error().message);
        return EXIT_FAILURE;
    }
    const Config &config = config_result.value();
    const nds::Result<nds::TcpAddress> address_result = nds::parse_tcp_address(config.listen);
    if (!address_result.ok()) {
        NDS_LOG_ERROR("verbs-server", "invalid listen address: {}", address_result.error().message);
        return EXIT_FAILURE;
    }
    const nds::TcpAddress &address = address_result.value();
    nds::server::Endpoint endpoint;
    const nds::Result<void> open_result = endpoint.open(config.endpoint);
    if (!open_result.ok()) {
        NDS_LOG_ERROR("verbs-server", "verbs endpoint open failed: {}", open_result.error().message);
        return EXIT_FAILURE;
    }
    auto qp_result = endpoint.create_qp();
    if (!qp_result.ok()) {
        NDS_LOG_ERROR("verbs-server", "QP creation failed: {}", qp_result.error().message);
        return EXIT_FAILURE;
    }
    nds::server::QueuePair &qp = qp_result.value();
    // The CPU side owns one receive MR and one posted receive for the client's Send.
    std::array<std::byte, 64U> payload{};
    const nds::Result<nds::server::MemoryRegion> region_result = endpoint.reg_mr(
        payload.data(), payload.size(), IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ);
    if (!region_result.ok()) {
        NDS_LOG_ERROR("verbs-server", "memory registration failed: {}", region_result.error().message);
        return EXIT_FAILURE;
    }
    const nds::server::MemoryRegion &region = region_result.value();
    const nds::Result<void> post_result = qp.post_receive(region);
    if (!post_result.ok()) {
        NDS_LOG_ERROR("verbs-server", "receive post failed: {}", post_result.error().message);
        return EXIT_FAILURE;
    }

    // Every local RoCE resource is ready. TCP now exchanges only the QP wire records.
    nds::Result<nds::TcpListener> listener_result = nds::TcpListener::listen(address.ipv4, address.port, 1);
    if (!listener_result.ok()) {
        NDS_LOG_ERROR("verbs-server", "listener open failed: {}", listener_result.error().message);
        return EXIT_FAILURE;
    }
    nds::TcpListener listener = std::move(listener_result).value();
    nds::Result<nds::TcpConnection> channel_result = listener.accept();
    if (!channel_result.ok()) {
        NDS_LOG_ERROR("verbs-server", "TCP accept failed: {}", channel_result.error().message);
        return EXIT_FAILURE;
    }
    nds::TcpConnection channel = std::move(channel_result).value();
    const nds::Result<nds::QpInfo> peer_result =
        nds::examples::verbs::exchange_server_qp(channel, qp.local_qp_info());
    if (!peer_result.ok()) {
        NDS_LOG_ERROR("verbs-server", "QP exchange failed: {}", peer_result.error().message);
        return EXIT_FAILURE;
    }
    const nds::Result<void> connect_result = qp.connect(peer_result.value());
    if (!connect_result.ok()) {
        NDS_LOG_ERROR("verbs-server", "QP connection failed: {}", connect_result.error().message);
        return EXIT_FAILURE;
    }
    // Do not let the client submit until the receive is armed.
    const nds::Result<void> barrier_result = nds::examples::verbs::send_barrier(channel);
    if (!barrier_result.ok()) {
        NDS_LOG_ERROR("verbs-server", "client barrier failed: {}", barrier_result.error().message);
        return EXIT_FAILURE;
    }
    const nds::Result<void> completion_result = qp.wait_receive(5000U);
    if (!completion_result.ok()) {
        NDS_LOG_ERROR("verbs-server", "receive completion failed: {}", completion_result.error().message);
        return EXIT_FAILURE;
    }
    const nds::Result<void> send_completion_barrier = nds::examples::verbs::send_barrier(channel);
    if (!send_completion_barrier.ok()) {
        NDS_LOG_ERROR("verbs-server", "send completion barrier failed: {}", send_completion_barrier.error().message);
        return EXIT_FAILURE;
    }
    // The client posts its receive before requesting this return Send.
    const nds::Result<void> receive_barrier_result = nds::examples::verbs::wait_barrier(channel);
    if (!receive_barrier_result.ok()) {
        NDS_LOG_ERROR("verbs-server", "client receive barrier failed: {}", receive_barrier_result.error().message);
        return EXIT_FAILURE;
    }
    // Use a distinct pattern so the final RDMA Write verifies a new payload.
    payload.fill(std::byte{0xa5});
    const nds::Result<void> send_result = qp.send(region, static_cast<std::uint32_t>(payload.size()), 5000U);
    if (!send_result.ok()) {
        NDS_LOG_ERROR("verbs-server", "return Send failed: {}", send_result.error().message);
        return EXIT_FAILURE;
    }
    const nds::Result<void> return_send_barrier = nds::examples::verbs::send_barrier(channel);
    if (!return_send_barrier.ok()) {
        NDS_LOG_ERROR("verbs-server", "return Send completion barrier failed: {}", return_send_barrier.error().message);
        return EXIT_FAILURE;
    }
    const nds::RemoteMemory remote_memory{reinterpret_cast<std::uint64_t>(region.address()),
                                          static_cast<std::uint32_t>(region.length()), region.remote_key()};
    const nds::Result<void> read_memory_result = nds::examples::verbs::send_remote_memory(channel, remote_memory);
    if (!read_memory_result.ok()) {
        NDS_LOG_ERROR("verbs-server", "RDMA Read remote-memory exchange failed: {}",
                      read_memory_result.error().message);
        return EXIT_FAILURE;
    }
    const nds::Result<void> read_complete_result = nds::examples::verbs::wait_barrier(channel);
    if (!read_complete_result.ok()) {
        NDS_LOG_ERROR("verbs-server", "RDMA Read completion acknowledgement failed: {}",
                      read_complete_result.error().message);
        return EXIT_FAILURE;
    }
    const nds::Result<void> write_memory_result = nds::examples::verbs::send_remote_memory(channel, remote_memory);
    if (!write_memory_result.ok()) {
        NDS_LOG_ERROR("verbs-server", "RDMA Write remote-memory exchange failed: {}",
                      write_memory_result.error().message);
        return EXIT_FAILURE;
    }
    const nds::Result<void> write_complete_result = nds::examples::verbs::wait_barrier(channel);
    if (!write_complete_result.ok()) {
        NDS_LOG_ERROR("verbs-server", "RDMA Write completion acknowledgement failed: {}",
                      write_complete_result.error().message);
        return EXIT_FAILURE;
    }
    if (!wait_for_payload(payload, std::byte{0xa5}, 5000U)) {
        NDS_LOG_ERROR("verbs-server", "RDMA Write did not update the server payload");
        return EXIT_FAILURE;
    }
    NDS_LOG_INFO("verbs-server", "completed direct verbs Send, Recv, Read, and configured RDMA Write");
    return EXIT_SUCCESS;
}

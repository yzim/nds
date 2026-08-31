#include "wire.hh"
#include "backend.hh"
#include "logging.hh"

#include <CLI/CLI.hpp>

#include <array>
#include <cstddef>
#include <cstdlib>
#include <string>

namespace {

struct Config {
    nds::server::BackendConfig backend;
    std::string listen{"0.0.0.0:18515"};
};

nds::Result<Config> parse(int argc, char **argv) {
    Config config;
    CLI::App app{"Receive a direct RA verbs Send with a TCP QP bootstrap."};
    app.add_option("--device", config.backend.device_name)->required();
    app.add_option("--gid-index", config.backend.gid_index)->required();
    app.add_option("--listen", config.listen);
    app.add_option("--ib-port", config.backend.port);
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
    nds::server::VerbsBackend backend;
    const nds::Result<void> open_result = backend.open(config.backend, 1U);
    if (!open_result.ok()) {
        NDS_LOG_ERROR("verbs-server", "verbs backend open failed: {}", open_result.error().message);
        return EXIT_FAILURE;
    }
    // The CPU side owns one receive MR and one posted receive for the client's Send.
    std::array<std::byte, 64U> payload{};
    const nds::Result<nds::server::RegisteredRegion> region_result =
        backend.register_memory(payload.data(), payload.size(), IBV_ACCESS_LOCAL_WRITE);
    if (!region_result.ok()) {
        NDS_LOG_ERROR("verbs-server", "memory registration failed: {}", region_result.error().message);
        return EXIT_FAILURE;
    }
    const nds::server::RegisteredRegion &region = region_result.value();
    const nds::Result<void> post_result = backend.post_receive(0U, region);
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
        nds::examples::verbs::exchange_server_qp(channel, backend.local_qp_infos().front());
    if (!peer_result.ok()) {
        NDS_LOG_ERROR("verbs-server", "QP exchange failed: {}", peer_result.error().message);
        return EXIT_FAILURE;
    }
    const nds::Result<void> connect_result = backend.connect({peer_result.value()});
    if (!connect_result.ok()) {
        NDS_LOG_ERROR("verbs-server", "QP connection failed: {}", connect_result.error().message);
        return EXIT_FAILURE;
    }
    // Do not let the client submit until the receive is armed.
    const nds::Result<void> ready_result = nds::examples::verbs::send_ready(channel);
    if (!ready_result.ok()) {
        NDS_LOG_ERROR("verbs-server", "client readiness failed: {}", ready_result.error().message);
        return EXIT_FAILURE;
    }
    const nds::Result<void> completion_result = backend.wait_receive(0U, 5000U);
    if (!completion_result.ok()) {
        NDS_LOG_ERROR("verbs-server", "receive completion failed: {}", completion_result.error().message);
        return EXIT_FAILURE;
    }
    NDS_LOG_INFO("verbs-server", "completed direct RA verbs receive");
    return EXIT_SUCCESS;
}

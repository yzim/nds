#include "direct.hh"
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
        return nds::unexpected(nds::ErrorCode::kInvalidArgument,
                               app.exit(error) == 0 ? "help requested" : "invalid options");
    }
    return config;
}

}  // namespace

int main(int argc, char **argv) {
    (void)nds::log::configure("verbs-server", "stderr", "info");
    const auto config = parse(argc, argv);
    if (!config) {
        NDS_LOG_ERROR("verbs-server", "options failed: {}", config.error().message);
        return EXIT_FAILURE;
    }
    const auto address = nds::parse_tcp_address(config->listen);
    if (!address) {
        NDS_LOG_ERROR("verbs-server", "invalid listen address: {}", address.error().message);
        return EXIT_FAILURE;
    }
    const auto listener = nds::TcpListener::listen(address->ipv4, address->port, 1);
    if (!listener) {
        NDS_LOG_ERROR("verbs-server", "listener open failed: {}", listener.error().message);
        return EXIT_FAILURE;
    }
    const auto channel = listener->accept();
    if (!channel) {
        NDS_LOG_ERROR("verbs-server", "TCP accept failed: {}", channel.error().message);
        return EXIT_FAILURE;
    }

    nds::server::VerbsBackend backend;
    if (const auto opened = backend.open(config->backend, 1U); !opened) {
        NDS_LOG_ERROR("verbs-server", "verbs backend open failed: {}", opened.error().message);
        return EXIT_FAILURE;
    }
    const auto peer = nds::examples::verbs::exchange_qp(&*channel, backend.local_qp_infos().front(), false);
    if (!peer) {
        NDS_LOG_ERROR("verbs-server", "QP exchange failed: {}", peer.error().message);
        return EXIT_FAILURE;
    }
    if (const auto connected = backend.connect({*peer}); !connected) {
        NDS_LOG_ERROR("verbs-server", "QP connection failed: {}", connected.error().message);
        return EXIT_FAILURE;
    }

    // The CPU side owns one receive MR and one posted receive for the client's Send.
    std::array<std::byte, 64U> payload{};
    const auto region = backend.register_memory(payload.data(), payload.size(), IBV_ACCESS_LOCAL_WRITE);
    if (!region) {
        NDS_LOG_ERROR("verbs-server", "memory registration failed: {}", region.error().message);
        return EXIT_FAILURE;
    }
    if (const auto posted = backend.post_receive(0U, *region); !posted) {
        NDS_LOG_ERROR("verbs-server", "receive post failed: {}", posted.error().message);
        return EXIT_FAILURE;
    }
    // Do not let the client submit until the receive is armed.
    if (const auto ready = nds::examples::verbs::send_ready(&*channel); !ready) {
        NDS_LOG_ERROR("verbs-server", "client readiness failed: {}", ready.error().message);
        return EXIT_FAILURE;
    }
    if (const auto completed = backend.wait_receive(0U, 5000U); !completed) {
        NDS_LOG_ERROR("verbs-server", "receive completion failed: {}", completed.error().message);
        return EXIT_FAILURE;
    }
    NDS_LOG_INFO("verbs-server", "completed direct RA verbs receive");
    return EXIT_SUCCESS;
}

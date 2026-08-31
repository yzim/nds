#include "wire.hh"
#include "verbs_wire.hh"
#include "backend.hh"
#include "logging.hh"

#include <CLI/CLI.hpp>

#include <array>
#include <cstddef>
#include <cstdlib>
#include <string>

int main(int argc, char **argv) {
    nds::server::BackendConfig config;
    std::string listen{"0.0.0.0:18515"};
    CLI::App app{"Run the direct one-QP, one-MR CPU verbs benchmark."};
    app.add_option("--device", config.device_name)->required();
    app.add_option("--gid-index", config.gid_index)->required();
    app.add_option("--listen", listen);
    app.add_option("--ib-port", config.port);
    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError &error) {
        return app.exit(error);
    }
    const auto address = nds::parse_tcp_address(listen);
    if (!address)
        return EXIT_FAILURE;
    nds::server::VerbsBackend backend;
    if (!backend.open(config, 1U))
        return EXIT_FAILURE;

    // Arm one receive against the benchmark's single CPU MR before notifying the client.
    std::array<std::byte, 64U> payload{};
    const auto region = backend.register_memory(payload.data(), payload.size(), IBV_ACCESS_LOCAL_WRITE);
    if (!region || !backend.post_receive(0U, *region))
        return EXIT_FAILURE;

    // TCP starts only after the local QP, MR, and receive WR are ready.
    const auto listener = nds::TcpListener::listen(address->ipv4, address->port, 1);
    if (!listener)
        return EXIT_FAILURE;
    const auto channel = listener->accept();
    if (!channel)
        return EXIT_FAILURE;
    const auto peer = nds::examples::verbs::exchange_server_qp(*channel, backend.local_qp_infos().front());
    if (!peer || !backend.connect({*peer}))
        return EXIT_FAILURE;
    if (!nds::examples::verbs::send_ready(*channel))
        return EXIT_FAILURE;
    if (const auto completed = backend.wait_receive(0U, 5000U); !completed)
        return EXIT_FAILURE;
    NDS_LOG_INFO("verbs-benchmark-server", "completed {} verbs operation",
                 nds::benchmark::operation_name(nds::benchmark::VerbsOperation::Send));
    return EXIT_SUCCESS;
}

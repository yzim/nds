#include "../examples/verbs/direct.hh"
#include "verbs_wire.hh"
#include "endpoint.hh"
#include "logging.hh"
#include "runtime.hh"

#include <CLI/CLI.hpp>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>

namespace {

struct Config {
    nds::client::RuntimeConfig runtime;
    nds::client::EndpointConfig endpoint;
    std::string server;
    nds::examples::verbs::BackendConfig backend;
};

nds::Result<Config> parse(int argc, char **argv) {
    Config config;
    CLI::App app{"Run the direct one-QP, one-MR NPU verbs benchmark."};
    app.add_option("--cann-runtime", config.runtime.cann_runtime_library)->required();
    app.add_option("--ra", config.endpoint.ra_library)->required();
    app.add_option("--ra-backend", config.backend.ra_backend);
    app.add_option("--logical-device", config.runtime.logical_device_id)->required();
    app.add_option("--server", config.server)->required();
    std::string backend_name{"ra"};
    app.add_option("--backend", backend_name, "Backend: ra, aiv, or aicpu");
    app.add_option("--aiv-kernel", config.backend.aiv_kernel);
    app.add_option("--aicpu-kernel", config.backend.aicpu_kernel);
    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError &error) {
        return nds::unexpected(nds::ErrorCode::kInvalidArgument,
                               app.exit(error) == 0 ? "help requested" : "invalid options");
    }
    if (backend_name == "ra")
        config.backend.mode = nds::client::NpuBackend::Ra;
    else if (backend_name == "aiv")
        config.backend.mode = nds::client::NpuBackend::Aiv;
    else if (backend_name == "aicpu")
        config.backend.mode = nds::client::NpuBackend::Aicpu;
    else
        return nds::unexpected(nds::ErrorCode::kInvalidArgument, "--backend must be ra, aiv, or aicpu");
    if (config.backend.mode == nds::client::NpuBackend::Aiv && config.backend.aiv_kernel.empty())
        return nds::unexpected(nds::ErrorCode::kInvalidArgument, "--aiv-kernel is required for AIV");
    if (config.backend.mode == nds::client::NpuBackend::Aicpu && config.backend.aicpu_kernel.empty())
        return nds::unexpected(nds::ErrorCode::kInvalidArgument, "--aicpu-kernel is required for AICPU");
    if (config.backend.mode == nds::client::NpuBackend::Ra && config.backend.ra_backend.empty())
        return nds::unexpected(nds::ErrorCode::kInvalidArgument, "--ra-backend is required for RA");
    return config;
}

}  // namespace

int main(int argc, char **argv) {
    (void)nds::log::configure("verbs-benchmark-client", "stderr", "info");
    const auto config = parse(argc, argv);
    if (!config)
        return EXIT_FAILURE;
    const auto address = nds::parse_tcp_address(config->server);
    if (!address)
        return EXIT_FAILURE;
    nds::client::Runtime runtime;
    nds::client::Endpoint endpoint;
    if (!runtime.open(config->runtime) || !endpoint.open(&runtime, config->endpoint))
        return EXIT_FAILURE;

    // The benchmark deliberately measures a single-QP, single-MR direct Send path.
    const nds::client::QueuePairConfig qp_config{};
    const auto created = endpoint.create_qp(qp_config, config->backend.mode);
    const auto channel = nds::TcpConnection::connect(address->ipv4, address->port, 5000U);
    if (!created || !channel)
        return EXIT_FAILURE;
    auto qp = std::move(*created);
    const auto device_backend = nds::examples::verbs::open_device_backend(&runtime, &qp, qp_config, config->backend);
    if (!device_backend)
        return EXIT_FAILURE;
    const auto local = qp.local_qp_info();
    if (!local)
        return EXIT_FAILURE;
    const auto peer = nds::examples::verbs::exchange_qp(&*channel, *local, true);
    if (!peer || !qp.connect(*peer))
        return EXIT_FAILURE;

    // Register one device buffer; setup is outside the operation itself.
    std::array<std::byte, 64U> payload{};
    const auto buffer = runtime.allocate(payload.size());
    if (!buffer)
        return EXIT_FAILURE;
    const auto region = endpoint.reg_mr(*buffer, nds::client::MemoryAccess::DirectNpu);
    if (!region)
        return EXIT_FAILURE;
    const NdsDeviceSendWr request{1U,
                                  NDS_DEVICE_WR_SEND,
                                  NDS_DEVICE_SEND_SIGNALED,
                                  {region->address(), static_cast<std::uint32_t>(payload.size()), region->local_key()},
                                  0U,
                                  0U,
                                  0U};
    // The server has posted its receive before sending this readiness byte.
    if (!nds::examples::verbs::wait_ready(&*channel))
        return EXIT_FAILURE;
    if (!nds::examples::verbs::submit_device(&runtime, &qp, *device_backend, request))
        return EXIT_FAILURE;
    // AI launchers synchronize their stream; RA additionally polls its host CQ.
    if (config->backend.mode == nds::client::NpuBackend::Ra &&
        !nds::examples::verbs::wait_ra_completion(device_backend->ra, &qp, true, 5000U))
        return EXIT_FAILURE;
    NDS_LOG_INFO("verbs-benchmark-client", "completed {} verbs operation",
                 nds::benchmark::operation_name(nds::benchmark::VerbsOperation::Send));
    return EXIT_SUCCESS;
}

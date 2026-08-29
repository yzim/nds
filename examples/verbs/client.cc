#include "direct.hh"
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
    nds::examples::verbs::BackendConfig backend;
    std::string server;
};

nds::Result<Config> parse(int argc, char **argv) {
    Config config;
    CLI::App app{"Exercise direct NPU verbs with a TCP QP bootstrap."};
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
    (void)nds::log::configure("verbs-client", "stderr", "info");
    const auto config = parse(argc, argv);
    if (!config) {
        NDS_LOG_ERROR("verbs-client", "options failed: {}", config.error().message);
        return EXIT_FAILURE;
    }
    const auto address = nds::parse_tcp_address(config->server);
    if (!address) {
        NDS_LOG_ERROR("verbs-client", "invalid server address: {}", address.error().message);
        return EXIT_FAILURE;
    }

    nds::client::Runtime runtime;
    nds::client::Endpoint endpoint;
    if (const auto opened = runtime.open(config->runtime); !opened) {
        NDS_LOG_ERROR("verbs-client", "runtime open failed: {}", opened.error().message);
        return EXIT_FAILURE;
    }
    if (const auto opened = endpoint.open(&runtime, config->endpoint); !opened) {
        NDS_LOG_ERROR("verbs-client", "endpoint open failed: {}", opened.error().message);
        return EXIT_FAILURE;
    }

    // Keep the workload to one QP, matching the single receive posted by the CPU peer.
    const nds::client::QueuePairConfig qp_config{};
    const auto created = endpoint.create_qp(qp_config, config->backend.mode);
    const auto channel = nds::TcpConnection::connect(address->ipv4, address->port, 5000U);
    if (!created) {
        NDS_LOG_ERROR("verbs-client", "QP creation failed: {}", created.error().message);
        return EXIT_FAILURE;
    }
    if (!channel) {
        NDS_LOG_ERROR("verbs-client", "TCP connection failed: {}", channel.error().message);
        return EXIT_FAILURE;
    }
    auto qp = std::move(*created);
    const auto device_backend = nds::examples::verbs::open_device_backend(&runtime, &qp, qp_config, config->backend);
    if (!device_backend) {
        NDS_LOG_ERROR("verbs-client", "backend setup failed: {}", device_backend.error().message);
        return EXIT_FAILURE;
    }
    const auto local = qp.local_qp_info();
    if (!local) {
        NDS_LOG_ERROR("verbs-client", "QP metadata failed: {}", local.error().message);
        return EXIT_FAILURE;
    }
    const auto peer = nds::examples::verbs::exchange_qp(&*channel, *local, true);
    if (!peer) {
        NDS_LOG_ERROR("verbs-client", "QP exchange failed: {}", peer.error().message);
        return EXIT_FAILURE;
    }
    if (const auto connected = qp.connect(*peer); !connected) {
        NDS_LOG_ERROR("verbs-client", "QP connection failed: {}", connected.error().message);
        return EXIT_FAILURE;
    }

    // Register exactly one device MR and use it for the Send WR.
    std::array<std::byte, 64U> payload{};
    const auto buffer = runtime.allocate(payload.size());
    if (!buffer) {
        NDS_LOG_ERROR("verbs-client", "NPU allocation failed: {}", buffer.error().message);
        return EXIT_FAILURE;
    }
    const auto region = endpoint.reg_mr(*buffer, nds::client::MemoryAccess::DirectNpu);
    if (!region) {
        NDS_LOG_ERROR("verbs-client", "memory registration failed: {}", region.error().message);
        return EXIT_FAILURE;
    }
    const NdsDeviceSendWr request{1U,
                                  NDS_DEVICE_WR_SEND,
                                  NDS_DEVICE_SEND_SIGNALED,
                                  {region->address(), static_cast<std::uint32_t>(payload.size()), region->local_key()},
                                  0U,
                                  0U,
                                  0U};
    // The server signals readiness only after its receive WR is posted.
    if (const auto ready = nds::examples::verbs::wait_ready(&*channel); !ready) {
        NDS_LOG_ERROR("verbs-client", "server readiness failed: {}", ready.error().message);
        return EXIT_FAILURE;
    }
    if (const auto submitted = nds::examples::verbs::submit_device(&runtime, &qp, *device_backend, request);
        !submitted) {
        NDS_LOG_ERROR("verbs-client", "Send failed: {}", submitted.error().message);
        return EXIT_FAILURE;
    }
    // Only RA exposes a host CQ poll in this direct example. AI modes are confirmed by the CPU peer.
    if (config->backend.mode == nds::client::NpuBackend::Ra) {
        if (const auto completed = nds::examples::verbs::wait_ra_completion(device_backend->ra, &qp, true, 5000U);
            !completed) {
            NDS_LOG_ERROR("verbs-client", "RA Send completion failed: {}", completed.error().message);
            return EXIT_FAILURE;
        }
    }
    NDS_LOG_INFO("verbs-client", "submitted direct {} verbs Send",
                 nds::examples::verbs::backend_name(config->backend.mode));
    return EXIT_SUCCESS;
}

#include "verbs_wire.hh"

#include "logging.hh"
#include "transport_protocol.hh"
#include "runtime.hh"
#include "transport.hh"

#include <CLI/CLI.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <span>
#include <string>

namespace {

constexpr std::uint32_t kMaximumInFlight = 16U;

struct Config {
    nds::client::RuntimeConfig runtime;
    nds::client::TransportConfig transport;
    nds::client::BackendConfig backend;
    nds::benchmark::VerbsOperation operation{nds::benchmark::VerbsOperation::Read};
    std::uint32_t bytes{};
    std::uint32_t in_flight{1U};
    std::uint32_t warmup{100U};
    std::uint32_t iterations{1000U};
};

nds::Result<Config> parse(int argc, char **argv) {
    Config config;
    std::string backend{"ra"};
    std::string operation{"read"};
    CLI::App app{"Benchmark NDS transport verbs submission."};
    app.add_option("--backend", backend)->required()->check(CLI::IsMember({"ra", "aiv", "aicpu"}));
    app.add_option("--cann-runtime", config.runtime.cann_runtime_library)->required();
    app.add_option("--ra", config.transport.endpoint.ra_library)->required();
    app.add_option("--aiv-kernel", config.backend.aiv_kernel);
    app.add_option("--aicpu-kernel-config", config.backend.aicpu_kernel_config);
    app.add_option("--logical-device", config.runtime.logical_device_id)->required();
    app.add_option("--server", config.transport.server_address)->required();
    app.add_option("--operation", operation)->required()->check(CLI::IsMember({"read", "write"}));
    app.add_option("--bytes", config.bytes)->required()->check(CLI::Range(1U, UINT32_MAX));
    app.add_option("--in-flight", config.in_flight)->check(CLI::Range(1U, kMaximumInFlight));
    app.add_option("--warmup", config.warmup)->check(CLI::Range(0U, UINT32_MAX));
    app.add_option("--iterations", config.iterations)->check(CLI::Range(1U, UINT32_MAX));
    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError &error) {
        return nds::unexpected(nds::ErrorCode::kInvalidArgument,
                               app.exit(error) == 0 ? "help requested" : "invalid options");
    }
    config.operation =
        operation == "read" ? nds::benchmark::VerbsOperation::Read : nds::benchmark::VerbsOperation::Write;
    if (backend == "aiv")
        config.backend.mode = nds::client::NpuBackend::Aiv;
    else if (backend == "aicpu")
        config.backend.mode = nds::client::NpuBackend::Aicpu;
    if ((config.backend.mode == nds::client::NpuBackend::Aiv && config.backend.aiv_kernel.empty()) ||
        (config.backend.mode == nds::client::NpuBackend::Aicpu && config.backend.aicpu_kernel_config.empty())) {
        return nds::unexpected(nds::ErrorCode::kInvalidArgument,
                               "selected device backend requires its kernel artifact");
    }
    config.transport.qp.send_queue_depth = 32U;
    if (config.backend.mode == nds::client::NpuBackend::Aicpu && config.in_flight != 1U) {
        return nds::unexpected(nds::ErrorCode::kUnsupported,
                               "AICPU batching requires a linked-provider transport implementation");
    }
    return config;
}

template <typename Request, typename Submit>
nds::Result<void> run(Submit submit, const nds::client::MemoryRegion &local, const nds::client::RemoteMemory &remote,
                      const Config &config, std::uint32_t count) {
    std::array<Request, kMaximumInFlight> requests{};
    for (std::uint32_t completed = 0U; completed < count;) {
        const std::uint32_t window = std::min(config.in_flight, count - completed);
        for (std::uint32_t index = 0U; index < window; ++index) {
            const std::uint64_t offset = static_cast<std::uint64_t>(index) * config.bytes;
            requests[index] = {&local, {remote.address + offset, remote.key, config.bytes}, config.bytes, offset};
        }
        if (const auto submitted = submit(std::span<const Request>{requests.data(), window}); !submitted)
            return nds::unexpected(submitted.error());
        completed += window;
    }
    return {};
}

template <typename Request, typename Submit>
nds::Result<void> measure(Submit submit, const nds::client::MemoryRegion &local,
                          const nds::client::RemoteMemory &remote, const Config &config,
                          std::chrono::steady_clock::duration *elapsed) {
    if (const auto warmed = run<Request>(submit, local, remote, config, config.warmup); !warmed)
        return nds::unexpected(warmed.error());
    const auto started = std::chrono::steady_clock::now();
    const auto measured = run<Request>(submit, local, remote, config, config.iterations);
    *elapsed = std::chrono::steady_clock::now() - started;
    return measured;
}

}  // namespace

int main(int argc, char **argv) {
    (void)nds::log::configure("verbs-benchmark-client", "stderr", "info");
    const auto config = parse(argc, argv);
    if (!config) {
        NDS_LOG_ERROR("verbs-benchmark-client", "options failed: {}", config.error().message);
        return EXIT_FAILURE;
    }
    nds::client::Runtime runtime;
    nds::client::Transport transport;
    if (const auto opened = runtime.open(config->runtime); !opened) {
        NDS_LOG_ERROR("verbs-benchmark-client", "runtime open failed: {}", opened.error().message);
        return EXIT_FAILURE;
    }
    if (const auto opened = transport.open(&runtime, config->transport, config->backend); !opened) {
        NDS_LOG_ERROR("verbs-benchmark-client", "transport setup failed: {}", opened.error().message);
        return EXIT_FAILURE;
    }
    const auto queue = transport.queue(0U);
    if (!queue) {
        NDS_LOG_ERROR("verbs-benchmark-client", "transport queue unavailable: {}", queue.error().message);
        return EXIT_FAILURE;
    }
    nds::wire::RemoteMemory wire_remote{};
    if (const auto received =
            transport.exchange_channel()->receive(std::as_writable_bytes(std::span{&wire_remote, 1U}));
        !received) {
        NDS_LOG_ERROR("verbs-benchmark-client", "remote-memory bootstrap failed: {}", received.error().message);
        return EXIT_FAILURE;
    }
    nds::transport::RemoteMemory decoded{};
    const std::uint64_t window_bytes = static_cast<std::uint64_t>(config->bytes) * config->in_flight;
    if (nds::transport::decode(&wire_remote, &decoded) != nds::transport::CodecResult::Ok ||
        decoded.length < window_bytes) {
        NDS_LOG_ERROR("verbs-benchmark-client", "server returned incompatible remote memory");
        return EXIT_FAILURE;
    }
    const auto buffer = runtime.allocate(static_cast<std::size_t>(window_bytes));
    if (!buffer) {
        NDS_LOG_ERROR("verbs-benchmark-client", "NPU allocation failed: {}", buffer.error().message);
        return EXIT_FAILURE;
    }
    const auto local = transport.register_memory(*buffer, nds::client::MemoryAccess::DirectNpu);
    if (!local) {
        NDS_LOG_ERROR("verbs-benchmark-client", "NPU buffer setup failed: {}", local.error().message);
        return EXIT_FAILURE;
    }
    const nds::client::RemoteMemory remote{decoded.address, decoded.remote_key, decoded.length};
    std::chrono::steady_clock::duration elapsed{};
    const auto completed = config->operation == nds::benchmark::VerbsOperation::Read
                               ? measure<nds::client::TransportRead>(
                                     [&transport, queue](std::span<const nds::client::TransportRead> requests) {
                                         return transport.read_batch(*queue, requests);
                                     },
                                     *local, remote, *config, &elapsed)
                               : measure<nds::client::TransportWrite>(
                                     [&transport, queue](std::span<const nds::client::TransportWrite> requests) {
                                         return transport.write_batch(*queue, requests);
                                     },
                                     *local, remote, *config, &elapsed);
    if (!completed) {
        NDS_LOG_ERROR("verbs-benchmark-client", "benchmark failed: {}", completed.error().message);
        return EXIT_FAILURE;
    }
    const std::uint8_t finished{1U};
    if (const auto sent = transport.exchange_channel()->send(std::as_bytes(std::span{&finished, 1U})); !sent) {
        NDS_LOG_ERROR("verbs-benchmark-client", "completion handshake failed: {}", sent.error().message);
        return EXIT_FAILURE;
    }
    const double seconds = std::chrono::duration<double>(elapsed).count();
    const char *backend = config->backend.mode == nds::client::NpuBackend::Ra    ? "ra"
                          : config->backend.mode == nds::client::NpuBackend::Aiv ? "aiv"
                                                                                 : "aicpu";
    std::cout << std::fixed << std::setprecision(3) << "{\"backend\":\"" << backend << "\",\"operation\":\""
              << nds::benchmark::operation_name(config->operation) << "\",\"bytes\":" << config->bytes
              << ",\"in_flight\":" << config->in_flight << ",\"warmup\":" << config->warmup
              << ",\"iterations\":" << config->iterations
              << ",\"completion_policy\":\"transport-final-signaled\",\"elapsed_ns\":"
              << std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count()
              << ",\"ops_per_second\":" << static_cast<double>(config->iterations) / seconds << ",\"gib_per_second\":"
              << static_cast<double>(config->iterations) * config->bytes / seconds / (1024.0 * 1024.0 * 1024.0) << "}"
              << std::endl;
    return EXIT_SUCCESS;
}

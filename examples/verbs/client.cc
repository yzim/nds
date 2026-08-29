#include "logging.hh"
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

constexpr std::size_t kPayloadBytes = 64U;
constexpr std::size_t kBatchCount = 2U;
using Payload = std::array<std::byte, kPayloadBytes>;
using BatchPayload = std::array<Payload, kBatchCount>;

struct Config {
    nds::client::RuntimeConfig runtime;
    nds::client::TransportConfig transport;
    nds::client::BackendConfig backend;
    std::string operation{"send"};
};

nds::Result<Config> parse(int argc, char **argv) {
    Config config;
    std::string backend{"ra"};
    CLI::App app{"Exercise NDS transport verbs requests."};
    app.add_option("--backend", backend)->required()->check(CLI::IsMember({"ra", "aiv", "aicpu"}));
    app.add_option("--cann-runtime", config.runtime.cann_runtime_library)->required();
    app.add_option("--ra", config.transport.endpoint.ra_library)->required();
    app.add_option("--aiv-kernel", config.backend.aiv_kernel);
    app.add_option("--aicpu-kernel-config", config.backend.aicpu_kernel_config);
    app.add_option("--logical-device", config.runtime.logical_device_id)->required();
    app.add_option("--server", config.transport.server_address)->required();
    app.add_option("--qp-count", config.transport.qp_count)->check(CLI::Range(1U, nds::wire::kMaxQpInfoBatch));
    app.add_option("--operation", config.operation)->check(CLI::IsMember({"send", "send-batch", "recv"}));
    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError &error) {
        return nds::unexpected(nds::ErrorCode::kInvalidArgument,
                               app.exit(error) == 0 ? "help requested" : "invalid options");
    }
    if (backend == "aiv")
        config.backend.mode = nds::client::NpuBackend::Aiv;
    else if (backend == "aicpu")
        config.backend.mode = nds::client::NpuBackend::Aicpu;
    if ((config.backend.mode == nds::client::NpuBackend::Aiv && config.backend.aiv_kernel.empty()) ||
        (config.backend.mode == nds::client::NpuBackend::Aicpu && config.backend.aicpu_kernel_config.empty())) {
        return nds::unexpected(nds::ErrorCode::kInvalidArgument, "device backend requires its kernel artifact");
    }
    if (config.operation == "send-batch" && config.backend.mode == nds::client::NpuBackend::Aicpu) {
        return nds::unexpected(nds::ErrorCode::kUnsupported,
                               "AICPU Send batches require a linked-provider implementation");
    }
    return config;
}

BatchPayload payloads() {
    BatchPayload payload{};
    for (std::size_t payload_index = 0U; payload_index < payload.size(); ++payload_index) {
        for (std::size_t byte_index = 0U; byte_index < payload[payload_index].size(); ++byte_index)
            payload[payload_index][byte_index] =
                static_cast<std::byte>((payload_index * kPayloadBytes + byte_index) ^ 0x5aU);
    }
    return payload;
}

nds::Result<void> send(nds::client::Runtime *runtime, nds::client::Transport *transport, nds::client::QueueHandle queue,
                       const BatchPayload &payload, bool batch) {
    auto buffer = runtime->allocate(sizeof(payload));
    if (!buffer)
        return nds::unexpected(buffer.error());
    if (const auto copied = runtime->copy_to(&*buffer, payload.data(), sizeof(payload)); !copied)
        return nds::unexpected(copied.error());
    const auto region = transport->register_memory(*buffer, nds::client::MemoryAccess::DirectNpu);
    if (!region)
        return nds::unexpected(region.error());
    if (batch) {
        const std::array requests{nds::client::TransportSend{&*region, kPayloadBytes, 0U},
                                  nds::client::TransportSend{&*region, kPayloadBytes, kPayloadBytes}};
        if (const auto submitted = transport->send_batch(queue, requests); !submitted)
            return nds::unexpected(submitted.error());
    } else if (const auto submitted = transport->send(queue, {&*region, kPayloadBytes}); !submitted) {
        return nds::unexpected(submitted.error());
    }
    return {};
}

nds::Result<void> receive(nds::client::Runtime *runtime, nds::client::Transport *transport,
                          nds::client::QueueHandle queue, const Payload &expected) {
    auto buffer = runtime->allocate(expected.size());
    if (!buffer)
        return nds::unexpected(buffer.error());
    const auto region = transport->register_memory(*buffer, nds::client::MemoryAccess::DirectNpu);
    if (!region)
        return nds::unexpected(region.error());
    if (const auto posted = transport->receive(queue, {&*region, static_cast<std::uint32_t>(expected.size())}); !posted)
        return nds::unexpected(posted.error());
    const std::uint8_t ready{1U};
    if (const auto sent = transport->exchange_channel()->send(std::as_bytes(std::span{&ready, 1U})); !sent)
        return nds::unexpected(sent.error());
    if (const auto completed = transport->wait_receive(queue); !completed)
        return nds::unexpected(completed.error());
    Payload observed{};
    if (const auto copied = runtime->copy_from(observed.data(), *buffer, observed.size()); !copied)
        return nds::unexpected(copied.error());
    return observed == expected ? nds::Result<void>{}
                                : nds::unexpected(nds::ErrorCode::kRuntime, "verbs receive payload mismatch");
}

}  // namespace

int main(int argc, char **argv) {
    (void)nds::log::configure("verbs-client", "stderr", "info");
    const auto config = parse(argc, argv);
    if (!config) {
        NDS_LOG_ERROR("verbs-client", "options failed: {}", config.error().message);
        return EXIT_FAILURE;
    }
    nds::client::Runtime runtime;
    nds::client::Transport transport;
    if (const auto opened = runtime.open(config->runtime); !opened) {
        NDS_LOG_ERROR("verbs-client", "runtime open failed: {}", opened.error().message);
        return EXIT_FAILURE;
    }
    if (const auto opened = transport.open(&runtime, config->transport, config->backend); !opened) {
        NDS_LOG_ERROR("verbs-client", "transport open failed: {}", opened.error().message);
        return EXIT_FAILURE;
    }
    const auto queue = transport.queue(0U);
    if (!queue) {
        NDS_LOG_ERROR("verbs-client", "transport queue unavailable: {}", queue.error().message);
        return EXIT_FAILURE;
    }
    const BatchPayload payload = payloads();
    const auto completed = config->operation == "recv"         ? receive(&runtime, &transport, *queue, payload.front())
                           : config->operation == "send-batch" ? send(&runtime, &transport, *queue, payload, true)
                                                               : send(&runtime, &transport, *queue, payload, false);
    if (!completed) {
        NDS_LOG_ERROR("verbs-client", "{} failed: {}", config->operation, completed.error().message);
        return EXIT_FAILURE;
    }
    NDS_LOG_INFO("verbs-client", "completed NDS verbs {}", config->operation);
    return EXIT_SUCCESS;
}

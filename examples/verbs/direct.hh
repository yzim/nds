#ifndef NDS_EXAMPLES_VERBS_DIRECT_HH
#define NDS_EXAMPLES_VERBS_DIRECT_HH

#include "endpoint.hh"
#include "runtime.hh"
#include "tcp_socket.hh"
#include "transport_protocol.hh"
#include "device_verbs.h"
#include "backends/aicpu/launcher.hh"
#include "backends/aiv/launcher.hh"
#include "backends/ra/launcher.hh"

#include <chrono>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <thread>

namespace nds::examples::verbs {

inline constexpr std::int32_t kKernelTimeoutMs = 5000;

struct BackendConfig {
    client::NpuBackend mode{client::NpuBackend::Ra};
    std::string ra_backend;
    std::string aiv_kernel;
    std::string aicpu_kernel;
};

inline const char *backend_name(client::NpuBackend backend) {
    switch (backend) {
        case client::NpuBackend::Ra:
            return "ra";
        case client::NpuBackend::Aicpu:
            return "aicpu";
        case client::NpuBackend::Aiv:
            return "aiv";
    }
    return "unknown";
}

struct DeviceBackend {
    BackendConfig config;
    RaLauncher ra;
    std::optional<client::MemoryBuffer> send_wr_ids;
    std::optional<client::MemoryBuffer> receive_wr_ids;
    std::unique_ptr<AivLauncher> aiv;
    std::unique_ptr<AicpuLauncher> aicpu;
};

inline Result<DeviceBackend> open_device_backend(client::Runtime *runtime, client::QueuePair *qp,
                                                 const client::QueuePairConfig &qp_config, BackendConfig config) {
    // AI-QP kernels read their queue metadata and WR-ID tables from device memory.
    DeviceBackend backend{.config = std::move(config)};
    if (backend.config.mode == client::NpuBackend::Ra) {
        if (backend.config.ra_backend.empty())
            return unexpected(ErrorCode::kInvalidArgument, "RA backend artifact path is required");
        if (const auto loaded = backend.ra.load(backend.config.ra_backend); !loaded)
            return unexpected(loaded.error());
        return backend;
    }

    auto send_wr_ids = runtime->allocate(qp_config.send_queue_depth * sizeof(std::uint64_t));
    if (!send_wr_ids)
        return unexpected(send_wr_ids.error());
    auto receive_wr_ids = runtime->allocate(qp_config.receive_queue_depth * sizeof(std::uint64_t));
    if (!receive_wr_ids)
        return unexpected(receive_wr_ids.error());
    if (const auto configured = qp->set_device_wr_id_storage(reinterpret_cast<std::uint64_t>(send_wr_ids->data()),
                                                             reinterpret_cast<std::uint64_t>(receive_wr_ids->data()));
        !configured) {
        return unexpected(configured.error());
    }
    backend.send_wr_ids = std::move(*send_wr_ids);
    backend.receive_wr_ids = std::move(*receive_wr_ids);

    // Loading resolves the artifact once; individual submissions resolve named entries lazily.
    if (backend.config.mode == client::NpuBackend::Aiv) {
        backend.aiv = std::make_unique<AivLauncher>();
        if (const auto loaded = backend.aiv->load(backend.config.aiv_kernel); !loaded)
            return unexpected(loaded.error());
    } else {
        backend.aicpu = std::make_unique<AicpuLauncher>();
        if (const auto loaded = backend.aicpu->load(backend.config.aicpu_kernel); !loaded)
            return unexpected(loaded.error());
    }
    return backend;
}

inline Result<void> submit_device(client::Runtime *runtime, client::QueuePair *qp, const DeviceBackend &backend,
                                  const NdsDeviceSendWr &wr) {
    // RA exposes a host-callable verbs API. AI backends receive the same WR through a device launch envelope.
    if (backend.config.mode == client::NpuBackend::Ra)
        return backend.ra.post_send(runtime, qp, wr);

    const auto device_qp = qp->make_device_transport();
    if (!device_qp)
        return unexpected(device_qp.error());
    NdsDevicePostSendArgs request{device_qp->control_qp, wr, std::numeric_limits<std::int32_t>::min()};
    // Stage the launch arguments in device-visible memory, then copy back the kernel result.
    auto request_buffer = runtime->allocate(sizeof(request));
    if (!request_buffer)
        return unexpected(request_buffer.error());
    if (const auto copied = runtime->copy_to(&*request_buffer, &request, sizeof(request)); !copied)
        return unexpected(copied.error());
    if (backend.config.mode == client::NpuBackend::Aicpu) {
        if (backend.aicpu == nullptr)
            return unexpected(ErrorCode::kRuntime, "AICPU launcher is unavailable");
        if (const auto launched = backend.aicpu->launch_and_wait(
                "nds_aicpu_post_send_kernel", reinterpret_cast<std::uint64_t>(request_buffer->data()),
                kKernelTimeoutMs);
            !launched)
            return unexpected(launched.error());
    } else {
        if (backend.aiv == nullptr)
            return unexpected(ErrorCode::kRuntime, "AIV launcher is unavailable");
        const std::uint64_t address = reinterpret_cast<std::uint64_t>(request_buffer->data());
        const std::array<std::uint64_t, 3U> arguments{address + offsetof(NdsDevicePostSendArgs, qp),
                                                      address + offsetof(NdsDevicePostSendArgs, wr),
                                                      address + offsetof(NdsDevicePostSendArgs, return_value)};
        if (const auto launched =
                backend.aiv->launch_and_wait("nds_aiv_post_send_kernel", const_cast<std::uint64_t *>(arguments.data()),
                                             sizeof(arguments), kKernelTimeoutMs);
            !launched)
            return unexpected(launched.error());
    }
    if (const auto copied = runtime->copy_from(&request, *request_buffer, sizeof(request)); !copied)
        return unexpected(copied.error());
    return request.return_value == 0 ? Result<void>{}
                                     : unexpected(ErrorCode::kRuntime, "device verbs submission failed");
}

inline Result<transport::QpInfo> exchange_qp(TcpConnection *channel, const transport::QpInfo &local, bool client) {
    // TCP carries only the NDS-owned, versioned QP record; provider handles stay local to each endpoint.
    wire::QpInfo local_record{};
    wire::QpInfo peer_record{};
    if (transport::encode(&local, &local_record) != transport::CodecResult::Ok)
        return unexpected(ErrorCode::kTransport, "cannot encode QP information");

    if (client) {
        if (const auto sent = channel->send(std::as_bytes(std::span{&local_record, 1U})); !sent)
            return unexpected(sent.error());
        if (const auto received = channel->receive(std::as_writable_bytes(std::span{&peer_record, 1U})); !received)
            return unexpected(received.error());
    } else {
        if (const auto received = channel->receive(std::as_writable_bytes(std::span{&peer_record, 1U})); !received)
            return unexpected(received.error());
        if (const auto sent = channel->send(std::as_bytes(std::span{&local_record, 1U})); !sent)
            return unexpected(sent.error());
    }

    transport::QpInfo peer{};
    if (transport::decode(&peer_record, &peer) != transport::CodecResult::Ok)
        return unexpected(ErrorCode::kTransport, "peer sent invalid QP information");
    return peer;
}

inline Result<void> wait_ra_completion(RaLauncher &launcher, client::QueuePair *qp, bool send_cq,
                                       std::uint32_t timeout_ms) {
    // RA owns a host-pollable CQ. AI kernel launches return after submission, so they use peer-side receive completion.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        NdsDeviceWc completion{};
        const auto polled = launcher.poll_cq(qp, send_cq, 1U, &completion);
        if (!polled)
            return unexpected(polled.error());
        if (*polled != 0U) {
            return completion.status == NDS_RA_WC_SUCCESS
                       ? Result<void>{}
                       : unexpected(ErrorCode::kRa, "RA completion reported failure");
        }
        std::this_thread::yield();
    }
    return unexpected(ErrorCode::kRa, "timed out waiting for RA completion");
}

inline Result<void> send_ready(TcpConnection *channel) {
    const std::uint8_t ready{1U};
    return channel->send(std::as_bytes(std::span{&ready, 1U}));
}

inline Result<void> wait_ready(TcpConnection *channel) {
    std::uint8_t ready{};
    if (const auto received = channel->receive(std::as_writable_bytes(std::span{&ready, 1U})); !received)
        return unexpected(received.error());
    if (ready != 1U)
        return unexpected(ErrorCode::kTransport, "peer sent an invalid readiness record");
    return {};
}

}  // namespace nds::examples::verbs

#endif

#include "aicpu/host/launcher.hh"
#include "aiv/host/launcher.hh"
#include "nds/device_verbs.h"
#include "nds/logging.hh"
#include "ra/ra.hh"
#include "runtime.hh"
#include "transport.hh"

#include <CLI/CLI.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>
#include <thread>
#include <utility>

namespace {

struct Config {
    nds::client::RuntimeConfig runtime;
    nds::client::TransportConfig transport;
    nds::client::BackendConfig backend;
    std::string operation{"send"};
    bool caller_polls_cq{};
};

constexpr std::size_t kPayloadBytes = 64U;
constexpr std::size_t kBatchCount = 2U;
using Payload = std::array<std::byte, kPayloadBytes>;
using BatchPayload = std::array<Payload, kBatchCount>;

struct AivThreeAddressArguments {
    std::uint64_t first_address;
    std::uint64_t second_address;
    std::uint64_t return_value_address;
};

struct AivPostSendBatchArguments {
    std::uint64_t qp_address;
    std::uint64_t wrs_address;
    std::uint32_t wr_count;
    std::uint32_t reserved;
    std::uint64_t bad_wr_address;
    std::uint64_t return_value_address;
};

struct AivPollCqArguments {
    std::uint64_t qp_address;
    std::uint32_t is_send_cq;
    std::uint32_t max_completions;
    std::uint64_t wc_address;
    std::uint64_t return_value_address;
};

nds::Result<Config> parse(int argc, char **argv) {
    Config config;
    CLI::App app{"Exercise one NDS verbs Send."};
    std::string backend{"ra"};
    app.add_option("--backend", backend, "Execution backend")->required()->check(CLI::IsMember({"ra", "aiv", "aicpu"}));
    app.add_option("--cann-runtime", config.runtime.cann_runtime_library)->required();
    app.add_option("--ra", config.transport.endpoint.ra_library)->required();
    app.add_option("--aiv-kernel", config.backend.aiv_kernel);
    app.add_option("--aicpu-kernel-config", config.backend.aicpu_kernel_config);
    app.add_option("--logical-device", config.runtime.logical_device_id)->required();
    app.add_option("--server", config.transport.server_address)->required();
    app.add_flag("--caller-polls-cq", config.caller_polls_cq, "Request caller-owned CQ dataplane memory");
    app.add_option("--operation", config.operation, "Verbs operation")
        ->check(CLI::IsMember({"send", "send-batch", "send-batch-invalid", "recv"}));
    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError &error) {
        return nds::unexpected(nds::ErrorCode::kInvalidArgument,
                               app.exit(error) == 0 ? "help requested" : "invalid options");
    }
    if (backend == "aiv")
        config.backend.mode = nds::client::NpuBackend::Aiv;
    if (backend == "aicpu")
        config.backend.mode = nds::client::NpuBackend::Aicpu;
    if ((config.backend.mode == nds::client::NpuBackend::Aiv && config.backend.aiv_kernel.empty()) ||
        (config.backend.mode == nds::client::NpuBackend::Aicpu &&
         config.backend.aicpu_kernel_config.empty())) {
        return nds::unexpected(nds::ErrorCode::kInvalidArgument, "device backend requires its kernel artifact");
    }
    if ((config.operation == "send-batch" || config.operation == "send-batch-invalid") &&
        config.backend.mode != nds::client::NpuBackend::Aiv) {
        return nds::unexpected(nds::ErrorCode::kInvalidArgument, "verbs Send batch requires the AIV backend");
    }
    if (config.caller_polls_cq)
        config.transport.qp.control_flags |= nds::client::QueuePairCallerPollsCq;
    return config;
}

nds::Result<void> post_send(nds::client::Runtime *runtime, nds::client::Transport *transport, const Payload &payload) {
    auto allocated = runtime->allocate(payload.size());
    if (!allocated)
        return nds::unexpected(allocated.error());
    nds::client::MemoryBuffer buffer = std::move(*allocated);
    if (const auto copied = runtime->copy_to(&buffer, payload.data(), payload.size()); !copied)
        return nds::unexpected(copied.error());
    auto registered = transport->endpoint()->reg_mr(buffer, nds::client::MemoryAccess::DirectNpu);
    if (!registered)
        return nds::unexpected(registered.error());
    nds::client::MemoryRegion region = std::move(*registered);
    const NdsDeviceSendWr wr{1U,
                             NDS_DEVICE_WR_SEND,
                             NDS_DEVICE_SEND_SIGNALED,
                             {region.address(), static_cast<std::uint32_t>(payload.size()), region.local_key()},
                             0U,
                             0U,
                             0U};
    if (transport->backend().mode == nds::client::NpuBackend::Ra) {
        return nds::NdsRaPostSend(runtime, transport->qp(), wr);
    }

    const auto device_transport = transport->qp()->make_device_transport();
    if (!device_transport)
        return nds::unexpected(device_transport.error());
    NdsDevicePostSendArgs request{};
    request.qp = device_transport->control_qp;
    request.wr = wr;
    request.return_value = std::numeric_limits<std::int32_t>::min();
    auto request_buffer = runtime->allocate(sizeof(request));
    if (!request_buffer)
        return nds::unexpected(request_buffer.error());
    if (const auto copied = runtime->copy_to(&*request_buffer, &request, sizeof(request)); !copied)
        return nds::unexpected(copied.error());
    const std::uint64_t request_address = reinterpret_cast<std::uint64_t>(request_buffer->data());
    if (transport->backend().mode == nds::client::NpuBackend::Aicpu) {
        nds::AicpuLauncher launcher;
        if (const auto loaded = launcher.load(transport->backend().aicpu_kernel_config); !loaded)
            return nds::unexpected(loaded.error());
        if (const auto launched = launcher.launch_and_wait("nds_aicpu_post_send_kernel", request_address, 5000); !launched)
            return nds::unexpected(launched.error());
    } else {
        nds::AivLauncher launcher;
        if (const auto loaded = launcher.load(transport->backend().aiv_kernel); !loaded)
            return nds::unexpected(loaded.error());
        AivThreeAddressArguments arguments{request_address + offsetof(NdsDevicePostSendArgs, qp),
                                            request_address + offsetof(NdsDevicePostSendArgs, wr),
                                            request_address + offsetof(NdsDevicePostSendArgs, return_value)};
        if (const auto launched =
                launcher.launch_and_wait("nds_aiv_post_send_kernel", &arguments, sizeof(arguments), 5000);
            !launched)
            return nds::unexpected(launched.error());
    }
    if (const auto copied = runtime->copy_from(&request, *request_buffer, sizeof(request)); !copied)
        return nds::unexpected(copied.error());
    return request.return_value == 0
               ? nds::Result<void>{}
               : nds::unexpected(nds::ErrorCode::kRuntime,
                                 "device verbs Send failed: " + std::to_string(request.return_value));
}

nds::Result<void> post_send_batch(nds::client::Runtime *runtime, nds::client::Transport *transport,
                                  const BatchPayload &payload, bool invalid_tail) {
    auto allocated = runtime->allocate(sizeof(payload));
    if (!allocated)
        return nds::unexpected(allocated.error());
    nds::client::MemoryBuffer buffer = std::move(*allocated);
    if (const auto copied = runtime->copy_to(&buffer, payload.data(), sizeof(payload)); !copied)
        return nds::unexpected(copied.error());
    auto registered = transport->endpoint()->reg_mr(buffer, nds::client::MemoryAccess::DirectNpu);
    if (!registered)
        return nds::unexpected(registered.error());
    nds::client::MemoryRegion region = std::move(*registered);
    const auto device_transport = transport->qp()->make_device_transport();
    if (!device_transport)
        return nds::unexpected(device_transport.error());

    std::array<NdsDeviceSendWr, kBatchCount> wrs{};
    for (std::size_t index = 0U; index < wrs.size(); ++index) {
        wrs[index] = {
            static_cast<std::uint64_t>(index + 1U),
            NDS_DEVICE_WR_SEND,
            NDS_DEVICE_SEND_SIGNALED,
            {region.address() + index * kPayloadBytes, static_cast<std::uint32_t>(kPayloadBytes), region.local_key()},
            0U,
            0U,
            0U};
    }
    if (invalid_tail)
        wrs.back().opcode = UINT32_MAX;
    auto wrs_buffer = runtime->allocate(sizeof(wrs));
    if (!wrs_buffer)
        return nds::unexpected(wrs_buffer.error());
    if (const auto copied = runtime->copy_to(&*wrs_buffer, wrs.data(), sizeof(wrs)); !copied)
        return nds::unexpected(copied.error());

    NdsDevicePostSendBatchArgs request{};
    request.qp = device_transport->control_qp;
    request.wrs_address = reinterpret_cast<std::uint64_t>(wrs_buffer->data());
    request.wr_count = static_cast<std::uint32_t>(wrs.size());
    request.return_value = std::numeric_limits<std::int32_t>::min();
    auto request_buffer = runtime->allocate(sizeof(request));
    if (!request_buffer)
        return nds::unexpected(request_buffer.error());
    if (const auto copied = runtime->copy_to(&*request_buffer, &request, sizeof(request)); !copied)
        return nds::unexpected(copied.error());

    nds::AivLauncher launcher;
    if (const auto loaded = launcher.load(transport->backend().aiv_kernel); !loaded)
        return nds::unexpected(loaded.error());
    const std::uint64_t request_address = reinterpret_cast<std::uint64_t>(request_buffer->data());
    AivPostSendBatchArguments arguments{request_address + offsetof(NdsDevicePostSendBatchArgs, qp), request.wrs_address,
                                         request.wr_count, 0U,
                                         request_address + offsetof(NdsDevicePostSendBatchArgs, bad_wr_address),
                                         request_address + offsetof(NdsDevicePostSendBatchArgs, return_value)};
    if (const auto launched =
            launcher.launch_and_wait("nds_aiv_post_send_batch_kernel", &arguments, sizeof(arguments), 5000);
        !launched) {
        return nds::unexpected(launched.error());
    }
    if (const auto copied = runtime->copy_from(&request, *request_buffer, sizeof(request)); !copied)
        return nds::unexpected(copied.error());
    const std::uint64_t expected_bad =
        invalid_tail ? reinterpret_cast<std::uint64_t>(wrs_buffer->data()) + sizeof(NdsDeviceSendWr) : 0U;
    if ((!invalid_tail && (request.return_value != 0 || request.bad_wr_address != 0U)) ||
        (invalid_tail &&
         (request.return_value != -NDS_DEVICE_OPERATION_INVALID_ARGUMENT || request.bad_wr_address != expected_bad))) {
        return nds::unexpected(nds::ErrorCode::kRuntime, "device verbs Send batch failed");
    }
    return {};
}

nds::Result<void> poll_cq(nds::client::Runtime *runtime, nds::client::Transport *transport, bool send_cq) {
    if (transport->backend().mode == nds::client::NpuBackend::Ra) {
        NdsDeviceWc completion{};
        for (std::uint32_t elapsed = 0U; elapsed < 5000U; elapsed += 10U) {
            const auto polled = nds::NdsRaPollCq(transport->qp(), send_cq, 1U, &completion);
            if (!polled)
                return nds::unexpected(polled.error());
            if (*polled != 0U)
                return {};
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return nds::unexpected(nds::ErrorCode::kRuntime, "timed out polling RA CQ");
    }
    auto completions = runtime->allocate(sizeof(NdsDeviceWc));
    if (!completions)
        return nds::unexpected(completions.error());
    const auto device_transport = transport->qp()->make_device_transport();
    if (!device_transport)
        return nds::unexpected(device_transport.error());
    for (std::uint32_t elapsed = 0U; elapsed < 5000U; elapsed += 10U) {
        NdsDevicePollCqArgs request{device_transport->control_qp, send_cq ? 1U : 0U, 1U,
                                    reinterpret_cast<std::uint64_t>(completions->data()),
                                    std::numeric_limits<std::int32_t>::min()};
        auto request_buffer = runtime->allocate(sizeof(request));
        if (!request_buffer)
            return nds::unexpected(request_buffer.error());
        if (const auto copied = runtime->copy_to(&*request_buffer, &request, sizeof(request)); !copied)
            return nds::unexpected(copied.error());
        const std::uint64_t request_address = reinterpret_cast<std::uint64_t>(request_buffer->data());
        if (transport->backend().mode == nds::client::NpuBackend::Aicpu) {
            nds::AicpuLauncher launcher;
            if (const auto loaded = launcher.load(transport->backend().aicpu_kernel_config);
                !loaded)
                return nds::unexpected(loaded.error());
            if (const auto launched = launcher.launch_and_wait("nds_aicpu_poll_cq_kernel", request_address, 5000);
                !launched)
                return nds::unexpected(launched.error());
        } else {
            nds::AivLauncher launcher;
            if (const auto loaded = launcher.load(transport->backend().aiv_kernel); !loaded)
                return nds::unexpected(loaded.error());
            AivPollCqArguments arguments{request_address + offsetof(NdsDevicePollCqArgs, qp), send_cq ? 1U : 0U, 1U,
                                          reinterpret_cast<std::uint64_t>(completions->data()),
                                          request_address + offsetof(NdsDevicePollCqArgs, return_value)};
            if (const auto launched =
                    launcher.launch_and_wait("nds_aiv_poll_cq_kernel", &arguments, sizeof(arguments), 5000);
                !launched)
                return nds::unexpected(launched.error());
        }
        if (const auto copied = runtime->copy_from(&request, *request_buffer, sizeof(request)); !copied)
            return nds::unexpected(copied.error());
        if (request.return_value > 0)
            return {};
        if (request.return_value < 0)
            return nds::unexpected(nds::ErrorCode::kRuntime,
                                   "device PollCq failed: " + std::to_string(request.return_value));
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return nds::unexpected(nds::ErrorCode::kRuntime, "timed out polling device CQ");
}

nds::Result<void> post_recv(nds::client::Runtime *runtime, nds::client::Transport *transport, const Payload &expected) {
    auto allocated = runtime->allocate(expected.size());
    if (!allocated)
        return nds::unexpected(allocated.error());
    nds::client::MemoryBuffer buffer = std::move(*allocated);
    auto registered = transport->endpoint()->reg_mr(buffer, nds::client::MemoryAccess::DirectNpu);
    if (!registered)
        return nds::unexpected(registered.error());
    nds::client::MemoryRegion region = std::move(*registered);
    const NdsDeviceRecvWr wr{1U, {region.address(), static_cast<std::uint32_t>(expected.size()), region.local_key()}};
    if (transport->backend().mode == nds::client::NpuBackend::Ra) {
        if (const auto posted = nds::NdsRaPostRecv(transport->qp(), wr); !posted)
            return nds::unexpected(posted.error());
    } else {
        const auto device_transport = transport->qp()->make_device_transport();
        if (!device_transport)
            return nds::unexpected(device_transport.error());
        NdsDevicePostRecvArgs request{device_transport->control_qp, wr, std::numeric_limits<std::int32_t>::min()};
        auto request_buffer = runtime->allocate(sizeof(request));
        if (!request_buffer)
            return nds::unexpected(request_buffer.error());
        if (const auto copied = runtime->copy_to(&*request_buffer, &request, sizeof(request)); !copied)
            return nds::unexpected(copied.error());
        const std::uint64_t request_address = reinterpret_cast<std::uint64_t>(request_buffer->data());
        if (transport->backend().mode == nds::client::NpuBackend::Aicpu) {
            nds::AicpuLauncher launcher;
            if (const auto loaded = launcher.load(transport->backend().aicpu_kernel_config);
                !loaded)
                return nds::unexpected(loaded.error());
            if (const auto launched = launcher.launch_and_wait("nds_aicpu_post_recv_kernel", request_address, 5000);
                !launched)
                return nds::unexpected(launched.error());
        } else {
            nds::AivLauncher launcher;
            if (const auto loaded = launcher.load(transport->backend().aiv_kernel); !loaded)
                return nds::unexpected(loaded.error());
            AivThreeAddressArguments arguments{request_address + offsetof(NdsDevicePostRecvArgs, qp),
                                                request_address + offsetof(NdsDevicePostRecvArgs, wr),
                                                request_address + offsetof(NdsDevicePostRecvArgs, return_value)};
            if (const auto launched =
                    launcher.launch_and_wait("nds_aiv_post_recv_kernel", &arguments, sizeof(arguments), 5000);
                !launched)
                return nds::unexpected(launched.error());
        }
        if (const auto copied = runtime->copy_from(&request, *request_buffer, sizeof(request)); !copied)
            return nds::unexpected(copied.error());
        if (request.return_value != 0)
            return nds::unexpected(nds::ErrorCode::kRuntime,
                                   "device PostRecv failed: " + std::to_string(request.return_value));
    }
    const std::uint8_t ready = 1U;
    if (const auto sent = transport->bootstrap()->send_bytes(&ready, sizeof(ready)); !sent)
        return nds::unexpected(sent.error());
    if (transport->backend().mode != nds::client::NpuBackend::Aicpu) {
        if (const auto completed = poll_cq(runtime, transport, false); !completed)
            return nds::unexpected(completed.error());
    }
    Payload observed{};
    if (const auto copied = runtime->copy_from(observed.data(), buffer, observed.size()); !copied)
        return nds::unexpected(copied.error());
    return observed == expected ? nds::Result<void>{}
                                : nds::unexpected(nds::ErrorCode::kRuntime, "verbs receive payload mismatch");
}

}  // namespace

int main(int argc, char **argv) {
    (void)nds::log::configure("verbs-client", "stderr", "info");
    const auto parsed = parse(argc, argv);
    if (!parsed) {
        NDS_LOG_ERROR("verbs-client", "options failed: {}", parsed.error().message);
        return EXIT_FAILURE;
    }
    nds::client::Runtime runtime;
    nds::client::Transport transport;
    if (const auto opened = runtime.open(parsed->runtime); !opened) {
        NDS_LOG_ERROR("verbs-client", "runtime open failed: {}", opened.error().message);
        return EXIT_FAILURE;
    }
    if (const auto opened = transport.open(&runtime, parsed->transport, parsed->backend); !opened) {
        NDS_LOG_ERROR("verbs-client", "transport open failed: {}", opened.error().message);
        return EXIT_FAILURE;
    }
    BatchPayload payload{};
    for (std::size_t payload_index = 0U; payload_index < payload.size(); ++payload_index) {
        for (std::size_t byte_index = 0U; byte_index < payload[payload_index].size(); ++byte_index)
            payload[payload_index][byte_index] =
                static_cast<std::byte>((payload_index * kPayloadBytes + byte_index) ^ 0x5aU);
    }
    nds::Result<void> completed;
    if (parsed->operation == "recv") {
        completed = post_recv(&runtime, &transport, payload[0]);
    } else if (parsed->operation == "send") {
        completed = post_send(&runtime, &transport, payload[0]);
        if (completed && parsed->backend.mode != nds::client::NpuBackend::Aicpu)
            completed = poll_cq(&runtime, &transport, true);
    } else {
        const bool invalid_tail = parsed->operation == "send-batch-invalid";
        completed = post_send_batch(&runtime, &transport, payload, invalid_tail);
        if (completed && parsed->backend.mode != nds::client::NpuBackend::Aicpu)
            completed = poll_cq(&runtime, &transport, true);
    }
    if (!completed) {
        NDS_LOG_ERROR("verbs-client", "{} failed: {}", parsed->operation, completed.error().message);
        return EXIT_FAILURE;
    }
    NDS_LOG_INFO("verbs-client", "completed NDS verbs {}", parsed->operation);
    return EXIT_SUCCESS;
}

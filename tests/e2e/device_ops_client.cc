#include "nds/logging.hh"
#include "nds/protocol.h"
#include "transport.hh"

#include <CLI/CLI.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <thread>

namespace {

constexpr std::uint64_t kReceiveWrId = UINT64_C(0x4e445352);
constexpr std::uint32_t kTimeoutMs = 5000U;

struct Config {
    nds::client::TransportConfig transport;
    std::string operation{"receive"};
    bool should_run{true};
};

int parse(int argc, char **argv, Config *config) {
    CLI::App app{"Exercise NPU send/receive posting and CQ polling against CPU verbs."};
    app.add_option("--ascendcl", config->transport.context.ascendcl_library)->required();
    app.add_option("--runtime", config->transport.context.runtime_library)->required();
    app.add_option("--ra", config->transport.context.ra_library)->required();
    std::string execution;
    app.add_option("--execution", execution)->required()->check(CLI::IsMember({"aicpu", "aiv"}));
    app.add_option("--operation", config->operation)
        ->check(CLI::IsMember({"send", "receive", "read", "write"}));
    app.add_option("--aicpu-kernel-config", config->transport.rma.aicpu_kernel_config);
    app.add_option("--aiv-kernel", config->transport.rma.aiv_kernel);
    app.add_option("--npu-ip", config->transport.qp.local_ipv4)->required();
    app.add_option("--logical-device", config->transport.context.logical_device_id)->required();
    app.add_option("--physical-device", config->transport.context.physical_device_id)->required();
    app.add_option("--cpu-ip", config->transport.cpu_ipv4)->required();
    app.add_option("--tcp-port", config->transport.tcp_port)->required();
    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError &error) {
        config->should_run = false;
        return app.exit(error);
    }
    config->transport.execution = execution == "aicpu" ? nds::NpuExecutionMode::Aicpu : nds::NpuExecutionMode::Aiv;
    config->transport.qp.physical_device_id = config->transport.context.physical_device_id;
    config->transport.tcp_timeout_ms = kTimeoutMs;
    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    (void)nds::log::configure("npu-client", "stderr", "info");
    Config config;
    if (const int result = parse(argc, argv, &config); result != 0)
        return result;
    if (!config.should_run)
        return EXIT_SUCCESS;
    nds::client::Transport transport;
    if (const auto opened = transport.open(config.transport); !opened) {
        NDS_LOG_ERROR("npu-client", "device-operation probe open failed: {}", opened.error().message);
        return EXIT_FAILURE;
    }
    nds::client::DeviceBuffer buffer;
    nds::client::RegisteredRegion region;
    if (const auto allocated = transport.allocate(64U, &buffer); !allocated) {
        NDS_LOG_ERROR("npu-client", "probe allocation failed: {}", allocated.error().message);
        return EXIT_FAILURE;
    }
    if (const auto registered = transport.register_memory(&buffer, &region); !registered) {
        NDS_LOG_ERROR("npu-client", "probe registration failed: {}", registered.error().message);
        return EXIT_FAILURE;
    }
    if (config.operation == "send") {
        std::array<std::uint8_t, 64U> payload{};
        payload[0] = 0x5aU;
        payload[63] = 0xa5U;
        if (const auto copied = transport.copy_to_device(&buffer, payload.data(), payload.size()); !copied) {
            NDS_LOG_ERROR("npu-client", "send payload copy failed: {}", copied.error().message);
            return EXIT_FAILURE;
        }
        const std::uint8_t ready = 1U;
        if (const auto sent = transport.bootstrap()->send_bytes(&ready, sizeof(ready)); !sent) {
            NDS_LOG_ERROR("npu-client", "probe readiness failed: {}", sent.error().message);
            return EXIT_FAILURE;
        }
        if (const auto sent = transport.send(region, payload.size()); !sent) {
            NDS_LOG_ERROR("npu-client", "post_send or send-CQ polling failed: {}", sent.error().message);
            return EXIT_FAILURE;
        }
        NDS_LOG_INFO("npu-client", "post_send and send-CQ polling completed one NPU SEND");
        return EXIT_SUCCESS;
    }
    if (config.operation == "read" || config.operation == "write") {
        nds_protocol_command_wire descriptor_wire{};
        if (const auto received = transport.bootstrap()->receive_bytes(&descriptor_wire, sizeof(descriptor_wire));
            !received) {
            NDS_LOG_ERROR("npu-client", "remote-memory descriptor receive failed: {}", received.error().message);
            return EXIT_FAILURE;
        }
        nds_protocol_command descriptor{};
        if (nds_protocol_command_decode(&descriptor_wire, &descriptor) != NDS_PROTOCOL_RESULT_OK) {
            NDS_LOG_ERROR("npu-client", "remote-memory descriptor is invalid");
            return EXIT_FAILURE;
        }
        std::array<std::uint8_t, 64U> payload{};
        payload[0] = 0x5aU;
        payload[63] = 0xa5U;
        if (config.operation == "write" &&
            !transport.copy_to_device(&buffer, payload.data(), payload.size())) {
            NDS_LOG_ERROR("npu-client", "RDMA Write payload copy failed");
            return EXIT_FAILURE;
        }
        const auto posted = config.operation == "read"
                                ? transport.read(region, descriptor.data.address, descriptor.data.rkey,
                                                 static_cast<std::uint32_t>(payload.size()))
                                : transport.write(region, descriptor.data.address, descriptor.data.rkey,
                                                  static_cast<std::uint32_t>(payload.size()));
        if (!posted) {
            NDS_LOG_ERROR("npu-client", "RDMA {} post failed: {}", config.operation, posted.error().message);
            return EXIT_FAILURE;
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kTimeoutMs);
        bool completed = false;
        while (std::chrono::steady_clock::now() < deadline) {
            nds_ra_completion completion{};
            const auto count = transport.poll(nds::CompletionQueue::Send, &completion, 1U);
            if (!count || (*count == 1U && completion.status != 0U)) {
                NDS_LOG_ERROR("npu-client", "RDMA {} send-CQ poll failed", config.operation);
                return EXIT_FAILURE;
            }
            if (*count == 1U) {
                completed = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (!completed) {
            NDS_LOG_ERROR("npu-client", "timed out waiting for RDMA {} completion", config.operation);
            return EXIT_FAILURE;
        }
        if (config.operation == "read") {
            std::array<std::uint8_t, 64U> received{};
            if (!transport.copy_from_device(received.data(), buffer, received.size()) || received != payload) {
                NDS_LOG_ERROR("npu-client", "RDMA Read payload verification failed");
                return EXIT_FAILURE;
            }
        }
        const std::uint8_t ready = 1U;
        if (const auto sent = transport.bootstrap()->send_bytes(&ready, sizeof(ready)); !sent) {
            NDS_LOG_ERROR("npu-client", "RDMA {} verification handshake failed", config.operation);
            return EXIT_FAILURE;
        }
        NDS_LOG_INFO("npu-client", "RDMA {} and send-CQ polling completed", config.operation);
        return EXIT_SUCCESS;
    }
    if (const auto posted = transport.post_receive(region, kReceiveWrId); !posted) {
        NDS_LOG_ERROR("npu-client", "post_recv failed: {}", posted.error().message);
        return EXIT_FAILURE;
    }
    const std::uint8_t ready = 1U;
    if (const auto sent = transport.bootstrap()->send_bytes(&ready, sizeof(ready)); !sent) {
        NDS_LOG_ERROR("npu-client", "probe readiness failed: {}", sent.error().message);
        return EXIT_FAILURE;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kTimeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        nds_ra_completion completion{};
        const auto count = transport.poll(nds::CompletionQueue::Receive, &completion, 1U);
        if (!count) {
            NDS_LOG_ERROR("npu-client", "receive CQ poll failed: {}", count.error().message);
            return EXIT_FAILURE;
        }
        if (*count == 1U) {
            std::array<std::uint8_t, 64U> payload{};
            if (completion.status != 0 || completion.wr_id != kReceiveWrId ||
                !transport.copy_from_device(payload.data(), buffer, payload.size()) ||
                payload[0] != 0x5aU || payload[63] != 0xa5U) {
                NDS_LOG_ERROR("npu-client", "receive completion or payload verification failed");
                return EXIT_FAILURE;
            }
            NDS_LOG_INFO("npu-client", "post_recv and receive-CQ polling completed one CPU SEND");
            return EXIT_SUCCESS;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    NDS_LOG_ERROR("npu-client", "timed out waiting for receive completion");
    return EXIT_FAILURE;
}

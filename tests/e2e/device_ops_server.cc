#include "nds/logging.hh"
#include "nds/protocol.h"
#include "transport.hh"

#include <CLI/CLI.hpp>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <string>

int main(int argc, char **argv) {
    (void)nds::log::configure("cpu-server", "stderr", "info");
    nds::server::ConnectionConfig config;
    CLI::App app{"Exercise CPU verbs against NPU send/receive posting and CQ polling."};
    app.add_option("--device", config.backend.device_name)->required();
    app.add_option("--gid-index", config.backend.gid_index)->required();
    app.add_option("--listen", config.listen_address)->required();
    app.add_option("--tcp-port", config.tcp_port)->required();
    std::string operation{"receive"};
    app.add_option("--operation", operation)->check(CLI::IsMember({"send", "receive", "read", "write"}));
    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError &error) {
        return app.exit(error);
    }
    nds::server::Connection connection;
    if (const auto opened = connection.open(config); !opened) {
        NDS_LOG_ERROR("cpu-server", "device-operation probe connection failed");
        return EXIT_FAILURE;
    }
    std::array<std::uint8_t, 64U> payload{};
    nds::server::RegisteredRegion region;
    if (operation == "send") {
        if (const auto prepared = connection.prepare_receive(payload.data(), payload.size(), &region); !prepared) {
            NDS_LOG_ERROR("cpu-server", "verbs receive preparation failed: {}", prepared.error().message);
            return EXIT_FAILURE;
        }
    }
    if (const auto activated = connection.activate(); !activated) {
        NDS_LOG_ERROR("cpu-server", "device-operation probe activation failed: {}", activated.error().message);
        return EXIT_FAILURE;
    }
    if (operation == "read" || operation == "write") {
        if (operation == "read") {
            payload[0] = 0x5aU;
            payload[63] = 0xa5U;
        }
        const auto access = operation == "read" ? nds::server::MemoryAccess::RemoteRead
                                                 : nds::server::MemoryAccess::RemoteWrite;
        if (const auto registered = connection.register_memory(payload.data(), payload.size(), access, &region);
            !registered) {
            NDS_LOG_ERROR("cpu-server", "RDMA {} memory registration failed: {}", operation,
                          registered.error().message);
            return EXIT_FAILURE;
        }
        const nds_protocol_command descriptor{
            1U,
            operation == "read" ? NDS_PROTOCOL_WRITE : NDS_PROTOCOL_READ,
            0U,
            payload.size(),
            {reinterpret_cast<std::uint64_t>(region.address()), payload.size(), region.remote_key(),
             operation == "read" ? NDS_PROTOCOL_ACCESS_REMOTE_READ : NDS_PROTOCOL_ACCESS_REMOTE_WRITE}};
        nds_protocol_command_wire descriptor_wire{};
        if (nds_protocol_command_encode(&descriptor, &descriptor_wire) != NDS_PROTOCOL_RESULT_OK ||
            !connection.bootstrap()->send_bytes(&descriptor_wire, sizeof(descriptor_wire))) {
            NDS_LOG_ERROR("cpu-server", "RDMA {} descriptor exchange failed", operation);
            return EXIT_FAILURE;
        }
        std::uint8_t ready{};
        if (const auto received = connection.bootstrap()->receive_bytes(&ready, sizeof(ready));
            !received || ready != 1U ||
            (operation == "write" && (payload[0] != 0x5aU || payload[63] != 0xa5U))) {
            NDS_LOG_ERROR("cpu-server", "RDMA {} payload verification failed", operation);
            return EXIT_FAILURE;
        }
        NDS_LOG_INFO("cpu-server", "RDMA {} payload verified", operation);
        return EXIT_SUCCESS;
    }
    std::uint8_t ready{};
    if (const auto received = connection.bootstrap()->receive_bytes(&ready, sizeof(ready)); !received || ready != 1U) {
        NDS_LOG_ERROR("cpu-server", "device-operation readiness failed");
        return EXIT_FAILURE;
    }
    if (operation == "send") {
        if (const auto received = connection.receive(5000U); !received || payload[0] != 0x5aU ||
            payload[63] != 0xa5U) {
            NDS_LOG_ERROR("cpu-server", "NPU SEND completion or payload verification failed");
            return EXIT_FAILURE;
        }
        NDS_LOG_INFO("cpu-server", "received and verified one NPU SEND");
        return EXIT_SUCCESS;
    }
    payload[0] = 0x5aU;
    payload[63] = 0xa5U;
    if (const auto registered = connection.register_memory(payload.data(), payload.size(),
                                                           nds::server::MemoryAccess::LocalRead, &region);
        !registered) {
        NDS_LOG_ERROR("cpu-server", "device-operation send registration failed: {}", registered.error().message);
        return EXIT_FAILURE;
    }
    if (const auto sent = connection.send(region, payload.size()); !sent) {
        NDS_LOG_ERROR("cpu-server", "verbs SEND failed: {}", sent.error().message);
        return EXIT_FAILURE;
    }
    NDS_LOG_INFO("cpu-server", "completed one verbs SEND to the NPU receive queue");
    return EXIT_SUCCESS;
}

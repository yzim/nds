#include "nds/control_plane.hpp"
#include "nds/npu_ra_context.hpp"
#include "nds/npu_ra_qp.hpp"

#include <arpa/inet.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <thread>

namespace {

struct ClientConfig {
    nds::NpuRaContextConfig context{};
    nds::NpuRaQpConfig qp{};
    std::string cpu_ipv4;
    std::uint16_t tcp_port{18515};
    std::uint32_t tcp_timeout_ms{10000};
    std::uint32_t hold_ms{1000};
    bool execute{false};
};

void usage(const char *program)
{
    std::cerr
        << "usage: " << program << " --ascendcl ABS_PATH --runtime ABS_PATH --ra ABS_PATH"
        << " --npu-ip IPV4 --logical-device ID --physical-device ID --cpu-ip IPV4 --execute"
        << " [--port PORT] [--path-mtu BYTES] [--tcp-port PORT] [--tcp-timeout-ms MS] [--hold-ms MS]\n"
        << "\n"
        << "Creates one direct NPU RA RC QP, exchanges QP metadata with one CPU verbs server,"
        << " calls RaTypicalQpModify, and posts no memory registration or work request."
        << " HCOMM/HCCL bootstrap and rank tables are intentionally not used.\n";
}

bool parse_u32(const char *text, std::uint32_t *value)
{
    char *end = nullptr;
    unsigned long parsed;

    if (text == nullptr || value == nullptr) return false;
    errno = 0;
    parsed = std::strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || parsed > std::numeric_limits<std::uint32_t>::max()) return false;
    *value = static_cast<std::uint32_t>(parsed);
    return true;
}

bool parse_args(int argc, char **argv, ClientConfig *config)
{
    for (int index = 1; index < argc; ++index) {
        const std::string argument(argv[index]);
        auto value = [&]() -> const char * { return ++index < argc ? argv[index] : nullptr; };
        std::uint32_t parsed = 0;

        if (argument == "--execute") {
            config->execute = true;
        } else if (argument == "--ascendcl") {
            const char *text = value(); if (text == nullptr) return false; config->context.ascendcl_library = text;
        } else if (argument == "--runtime") {
            const char *text = value(); if (text == nullptr) return false; config->context.runtime_library = text;
        } else if (argument == "--ra") {
            const char *text = value(); if (text == nullptr) return false; config->context.ra_library = text;
        } else if (argument == "--npu-ip") {
            const char *text = value(); if (text == nullptr) return false; config->qp.local_ipv4 = text;
        } else if (argument == "--logical-device") {
            const char *text = value(); if (text == nullptr || !parse_u32(text, &config->context.logical_device_id)) return false;
        } else if (argument == "--physical-device") {
            const char *text = value(); if (text == nullptr || !parse_u32(text, &config->context.physical_device_id)) return false;
            config->qp.physical_device_id = config->context.physical_device_id;
        } else if (argument == "--port") {
            const char *text = value(); if (text == nullptr || !parse_u32(text, &parsed) || parsed == 0U || parsed > UINT16_MAX) return false;
            config->qp.port_num = static_cast<std::uint16_t>(parsed);
        } else if (argument == "--path-mtu") {
            const char *text = value(); if (text == nullptr || !parse_u32(text, &parsed) || parsed == 0U || parsed > UINT16_MAX) return false;
            config->qp.path_mtu = static_cast<std::uint16_t>(parsed);
        } else if (argument == "--cpu-ip") {
            const char *text = value(); if (text == nullptr) return false; config->cpu_ipv4 = text;
        } else if (argument == "--tcp-port") {
            const char *text = value(); if (text == nullptr || !parse_u32(text, &parsed) || parsed == 0U || parsed > UINT16_MAX) return false;
            config->tcp_port = static_cast<std::uint16_t>(parsed);
        } else if (argument == "--tcp-timeout-ms") {
            const char *text = value(); if (text == nullptr || !parse_u32(text, &config->tcp_timeout_ms) || config->tcp_timeout_ms == 0U) return false;
        } else if (argument == "--hold-ms") {
            const char *text = value(); if (text == nullptr || !parse_u32(text, &config->hold_ms)) return false;
        } else {
            return false;
        }
    }
    return config->execute && !config->context.ascendcl_library.empty() && !config->context.runtime_library.empty() &&
           !config->context.ra_library.empty() && !config->qp.local_ipv4.empty() && !config->cpu_ipv4.empty();
}

void format_gid(const std::uint8_t gid[NDS_GID_BYTES], char text[INET6_ADDRSTRLEN])
{
    if (inet_ntop(AF_INET6, gid, text, INET6_ADDRSTRLEN) == nullptr) {
        std::strncpy(text, "<invalid>", INET6_ADDRSTRLEN);
        text[INET6_ADDRSTRLEN - 1] = '\0';
    }
}

void print_endpoint(const char *label, const nds_rc_endpoint &endpoint)
{
    char gid[INET6_ADDRSTRLEN]{};
    format_gid(endpoint.gid, gid);
    std::cout << label << " endpoint: phase="
              << ((endpoint.flags & NDS_ENDPOINT_FLAG_QP_ONLY) != 0U ? "QP-only" : "data-ready")
              << " qpn=" << endpoint.qp_num << " psn=" << endpoint.psn
              << " port=" << endpoint.port_num << " gid_index=" << endpoint.gid_index
              << " gid=" << gid << " path_mtu=" << endpoint.path_mtu
              << " tc=" << endpoint.traffic_class << " sl=" << endpoint.service_level
              << " retry_count=" << endpoint.retry_count << " retry_timeout=" << endpoint.retry_timeout << '\n';
}

} // namespace

int main(int argc, char **argv)
{
    ClientConfig config;
    nds::NpuRaContext context;
    nds::NpuRaQp qp;
    nds::TcpControlPlane control;
    nds_rc_endpoint local{};
    std::string control_error;

    if (!parse_args(argc, argv, &config)) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }
    if (!context.initialize(config.context)) {
        std::cerr << "direct NPU RA context initialization failed: " << context.error() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "Direct NPU RA context initialized: logical_device=" << config.context.logical_device_id
              << " physical_device=" << config.context.physical_device_id << '\n';
    if (!qp.create(context.ra_api(), config.qp)) {
        std::cerr << "NPU RA QP creation failed: " << qp.error() << '\n';
        return EXIT_FAILURE;
    }
    if (!qp.make_qp_only_endpoint(local)) {
        std::cerr << "cannot create NPU QP-only endpoint: " << qp.error() << '\n';
        return EXIT_FAILURE;
    }
    print_endpoint("NPU local", local);
    std::cout << "NPU RA QP capacity/depth: not exposed by the installed RaGetQpAttr ABI; "
              << "no send or receive WR is posted in this milestone.\n";

    if (!nds::TcpControlPlane::connect(config.cpu_ipv4, config.tcp_port, config.tcp_timeout_ms, control,
                                       &control_error)) {
        std::cerr << "TCP control-plane connect failed: " << control_error << '\n';
        return EXIT_FAILURE;
    }
    const nds::ControlPlaneResult exchanged = control.exchange_as_client(local);
    if (!exchanged.ok) {
        std::cerr << "QP endpoint exchange failed: " << exchanged.error << '\n';
        return EXIT_FAILURE;
    }
    if ((exchanged.peer.flags & NDS_ENDPOINT_FLAG_QP_ONLY) == 0U) {
        std::cerr << "CPU returned a non-QP-only endpoint; refusing to start data-plane work\n";
        return EXIT_FAILURE;
    }
    print_endpoint("CPU remote", exchanged.peer);
    if (!qp.connect(exchanged.peer)) {
        std::cerr << "RaTypicalQpModify failed: " << qp.error() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "RaTypicalQpModify succeeded: NPU RA RC QP accepted exchanged CPU QPN/GID/PSN metadata.\n";
    std::cout << "NPU-side QP establishment completed; holding TCP control connection for "
              << config.hold_ms << " ms, then closing it.\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(config.hold_ms));
    return EXIT_SUCCESS;
}

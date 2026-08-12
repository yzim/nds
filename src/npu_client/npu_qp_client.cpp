#include "nds/aicpu_roce.hpp"
#include "nds/control_plane.hpp"
#include "nds/npu_ra_context.hpp"
#include "nds/npu_ra_qp.hpp"

#include <arpa/inet.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr std::uint64_t kMaxTransferBytes = 64U * 1024U;
constexpr std::uint32_t kCompletionTimeoutMs = 5000U;
constexpr std::uint32_t kCqeErrorCapacity = 128U;
constexpr std::uint32_t kMaxQpOnlyHoldMs = 60000U;

struct ClientConfig {
    nds::NpuRaContextConfig context{};
    nds::NpuRaQpConfig qp{};
    std::string cpu_ipv4;
    std::uint16_t tcp_port{18515};
    std::uint32_t tcp_timeout_ms{10000};
    bool execute{false};
    bool qp_only{false};
    std::uint32_t qp_only_hold_ms{0};
    std::string aicpu_kernel_config;
};

void usage(const char *program)
{
    std::cerr
        << "usage: " << program << " --ascendcl ABS_PATH --runtime ABS_PATH --ra ABS_PATH"
        << " --npu-ip IPV4 --logical-device ID --physical-device ID --cpu-ip IPV4 --execute"
        << " [--submission-mode host-ra|aicpu] [--aicpu-kernel-config ABS_PATH]"
        << " [--qp-only] [--qp-only-hold-ms MS] [--port PORT] [--path-mtu BYTES] [--tcp-port PORT] [--tcp-timeout-ms MS]\n\n"
        << "Creates one NPU0 RA RC QP and exchanges QP metadata with one CPU verbs server. By default it"
        << " receives one bounded CPU destination-MR descriptor and submits exactly one RDMA Write;"
        << " host-ra uses RaTypicalSendWr/rtRDMADBSend/RaPollCq; aicpu uses CANN 9.0.0 RunTransportRoceTx"
        << " and requires a CPU --aicpu-sync peer plus explicit ccl_kernel.json. --qp-only validates QP establishment and posts no memory registration or work request;"
        << " --qp-only-hold-ms keeps that QP alive briefly for passive diagnostics."
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
        } else if (argument == "--qp-only") {
            config->qp_only = true;
        } else if (argument == "--qp-only-hold-ms") {
            const char *text = value();
            if (text == nullptr || !parse_u32(text, &config->qp_only_hold_ms) ||
                config->qp_only_hold_ms > kMaxQpOnlyHoldMs) return false;
        } else if (argument == "--ascendcl") {
            const char *text = value(); if (text == nullptr) return false; config->context.ascendcl_library = text;
        } else if (argument == "--runtime") {
            const char *text = value(); if (text == nullptr) return false; config->context.runtime_library = text;
        } else if (argument == "--ra") {
            const char *text = value(); if (text == nullptr) return false; config->context.ra_library = text;
        } else if (argument == "--submission-mode") {
            const char *text = value();
            if (text == nullptr) return false;
            if (std::strcmp(text, "host-ra") == 0) config->qp.submission_mode = nds::NpuRaSubmissionMode::HostRa;
            else if (std::strcmp(text, "aicpu") == 0) config->qp.submission_mode = nds::NpuRaSubmissionMode::Aicpu;
            else return false;
        } else if (argument == "--aicpu-kernel-config") {
            const char *text = value(); if (text == nullptr || text[0] == '\0') return false;
            config->aicpu_kernel_config = text;
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
        } else {
            return false;
        }
    }
    return config->execute && (config->qp_only || config->qp_only_hold_ms == 0U) &&
           !config->context.ascendcl_library.empty() && !config->context.runtime_library.empty() &&
           !config->context.ra_library.empty() && !config->qp.local_ipv4.empty() && !config->cpu_ipv4.empty() &&
           (config->qp.submission_mode != nds::NpuRaSubmissionMode::Aicpu || !config->aicpu_kernel_config.empty());
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
    std::cout << label << " endpoint: qpn=" << endpoint.qp_num << " psn=" << endpoint.psn
              << " port=" << endpoint.port_num << " gid_index=" << endpoint.gid_index
              << " gid=" << gid << " path_mtu=" << endpoint.path_mtu
              << " tc=" << endpoint.traffic_class << " sl=" << endpoint.service_level << '\n';
}

const char *qp_status_name(int status)
{
    switch (status) {
    case NDS_RA_QP_STATUS_NOT_CONNECTED: return "not-connected";
    case NDS_RA_QP_STATUS_CONNECTED: return "connected";
    case NDS_RA_QP_STATUS_TIMEOUT: return "timeout";
    case NDS_RA_QP_STATUS_CONNECTING: return "connecting";
    default: return "unknown";
    }
}

const char *port_status_name(int status)
{
    switch (status) {
    case NDS_RA_PORT_STATUS_DOWN: return "down";
    case NDS_RA_PORT_STATUS_ACTIVE: return "active";
    default: return "unknown";
    }
}

const char *completion_status_name(int status)
{
    switch (status) {
    case NDS_RA_WC_SUCCESS: return "success";
    case NDS_RA_WC_RETRY_EXCEEDED: return "retry-exceeded";
    default: return "unknown";
    }
}

bool print_port_status(nds::NpuRaQp &qp, const char *stage)
{
    int status = -1;
    if (!qp.query_port_status(status)) {
        std::cerr << "RaRdevGetPortStatus(" << stage << ") failed: " << qp.error() << '\n';
        return false;
    }
    std::cout << "NPU rdev port status " << stage << ": " << port_status_name(status) << " (" << status << ")\n";
    if (status != NDS_RA_PORT_STATUS_ACTIVE) {
        std::cerr << "NPU rdev port is not active at " << stage << "; refusing to continue toward the RDMA Write\n";
        return false;
    }
    return true;
}

const char *lite_support_name(int support_lite)
{
    switch (support_lite) {
    case NDS_RA_LITE_NOT_SUPPORTED: return "not-supported";
    case NDS_RA_LITE_ALIGN_4K: return "4K-aligned";
    case NDS_RA_LITE_ALIGN_2M: return "2M-aligned";
    default: return "unknown";
    }
}

bool print_lite_support(nds::NpuRaQp &qp)
{
    int support_lite = -1;
    if (!qp.query_support_lite(support_lite)) {
        std::cerr << "RaRdevGetSupportLite failed: " << qp.error() << '\n';
        return false;
    }
    std::cout << "NPU rdev RDMA-lite support: " << lite_support_name(support_lite)
              << " (" << support_lite << ")\n";
    if (support_lite == NDS_RA_LITE_NOT_SUPPORTED) {
        std::cerr << "NPU rdev does not support the required RDMA-lite send path; refusing to continue\n";
        return false;
    }
    return true;
}

bool print_qp_status(nds::NpuRaQp &qp, const char *stage)
{
    int status = -1;
    if (!qp.query_status(status)) {
        std::cerr << "RaGetQpStatus(" << stage << ") failed: " << qp.error() << '\n';
        return false;
    }
    std::cout << "NPU QP status " << stage << ": " << qp_status_name(status) << " (" << status << ")\n";
    if (status != NDS_RA_QP_STATUS_CONNECTED) {
        std::cerr << "NPU QP is not connected at " << stage << "; refusing to post the RDMA Write\n";
        return false;
    }
    return true;
}

void print_failure_diagnostics(nds::NpuRaQp &qp)
{
    int status = -1;
    if (qp.query_status(status)) {
        std::cerr << "NPU QP status after write failure: " << qp_status_name(status) << " (" << status << ")\n";
    } else {
        std::cerr << "RaGetQpStatus(after write failure) failed: " << qp.error() << '\n';
    }
    if (qp.query_port_status(status)) {
        std::cerr << "NPU rdev port status after write failure: " << port_status_name(status) << " (" << status << ")\n";
    } else {
        std::cerr << "RaRdevGetPortStatus(after write failure) failed: " << qp.error() << '\n';
    }

    std::array<nds_ra_cqe_error, kCqeErrorCapacity> errors{};
    std::uint32_t count = kCqeErrorCapacity;
    if (!qp.query_cqe_errors(errors.data(), count)) {
        std::cerr << "RaRdevGetCqeErrInfoList(after write failure) failed: " << qp.error() << '\n';
        return;
    }
    std::cerr << "NPU CQE error records consumed after write failure: " << count << '\n';
    for (std::uint32_t index = 0; index < count; ++index) {
        const nds_ra_cqe_error &error = errors[index];
        std::cerr << "  cqe_error[" << index << "]: status=" << error.status << " qpn=" << error.qp_number
                  << " time=" << static_cast<long long>(error.time.tv_sec) << "." << error.time.tv_usec << '\n';
    }
}

bool wait_for_send_completion(nds::NpuRaQp &qp)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kCompletionTimeoutMs);
    nds_ra_completion completion{};
    while (std::chrono::steady_clock::now() < deadline) {
        const int count = qp.poll_send_completions(&completion, 1U);
        if (count < 0) {
            std::cerr << "RaPollCq(send) failed: " << qp.error() << '\n';
            return false;
        }
        if (count > 0) {
            std::cout << "NPU send completion: status=" << completion_status_name(completion.status)
                      << " (" << completion.status << ") opcode=" << completion.opcode
                      << " qpn=" << completion.qp_number << " bytes=" << completion.byte_length
                      << " vendor_error=" << completion.vendor_error << '\n';
            return completion.status == NDS_RA_WC_SUCCESS;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::cerr << "timed out waiting " << kCompletionTimeoutMs << " ms for the signaled NPU RDMA Write completion\n";
    return false;
}

} // namespace

int main(int argc, char **argv)
{
    ClientConfig config;
    nds::NpuRaContext context;
    nds::NpuRaQp qp;
    nds::AicpuRoceTxLauncher aicpu_launcher;
    nds::TcpControlPlane control;
    nds_rc_endpoint local{};
    nds_memory_descriptor destination{};
    nds_memory_descriptor aicpu_sync{};
    nds_ra_mr_info source_mr{};
    nds_ra_mr_info local_sync_mr{};
    nds_ra_send_response response{};
    void *device_buffer = nullptr;
    void *local_sync_buffer = nullptr;
    void *mr_handle = nullptr;
    void *local_sync_mr_handle = nullptr;
    bool ok = false;
    std::string control_error;

    if (!parse_args(argc, argv, &config)) { usage(argv[0]); return EXIT_FAILURE; }
    if (!context.initialize(config.context)) { std::cerr << "direct NPU RA context initialization failed: " << context.error() << '\n'; return EXIT_FAILURE; }
    std::cout << "Direct NPU RA context initialized: logical_device=" << config.context.logical_device_id
              << " physical_device=" << config.context.physical_device_id << '\n';
    if (!qp.create(context.ra_api(), config.qp) || !qp.make_qp_only_endpoint(local)) {
        std::cerr << "NPU RA QP setup failed: " << qp.error() << '\n'; return EXIT_FAILURE;
    }
    print_endpoint("NPU local", local);
    if (!print_port_status(qp, "after rdev/QP create") || !print_lite_support(qp)) return EXIT_FAILURE;
    if (!nds::TcpControlPlane::connect(config.cpu_ipv4, config.tcp_port, config.tcp_timeout_ms, control, &control_error)) {
        std::cerr << "TCP control-plane connect failed: " << control_error << '\n'; return EXIT_FAILURE;
    }
    const nds::ControlPlaneResult exchanged = control.exchange_as_client(local);
    if (!exchanged.ok || (exchanged.peer.flags & NDS_ENDPOINT_FLAG_QP_ONLY) == 0U) {
        std::cerr << "QP endpoint exchange failed: " << exchanged.error << '\n'; return EXIT_FAILURE;
    }
    print_endpoint("CPU remote", exchanged.peer);
    if (!qp.connect(exchanged.peer)) { std::cerr << "RaTypicalQpModify failed: " << qp.error() << '\n'; return EXIT_FAILURE; }
    std::cout << "RaTypicalQpModify succeeded.\n";
    if (!print_qp_status(qp, "after modify") || !print_port_status(qp, "after modify")) return EXIT_FAILURE;
    if (config.qp_only) {
        if (config.qp_only_hold_ms != 0U) {
            std::cout << "Holding the QP-only connection for " << config.qp_only_hold_ms
                      << " ms for passive diagnostics; no memory registration or RDMA work request will be posted.\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(config.qp_only_hold_ms));
        }
        std::cout << "QP-only validation passed; no memory registration or RDMA work request was posted.\n";
        return EXIT_SUCCESS;
    }
    if (!control.receive_memory_descriptor(destination, &control_error)) {
        std::cerr << "CPU destination-MR exchange failed: " << control_error << '\n'; return EXIT_FAILURE;
    }
    if ((destination.access_flags & NDS_MEMORY_ACCESS_REMOTE_WRITE) == 0U || destination.length == 0U ||
        destination.length > kMaxTransferBytes || destination.length > std::numeric_limits<std::uint32_t>::max()) {
        std::cerr << "CPU advertised an unsupported destination-MR descriptor\n"; return EXIT_FAILURE;
    }
    std::cout << "CPU destination MR: transaction=" << destination.transaction_id << " addr=0x" << std::hex
              << destination.address << std::dec << " bytes=" << destination.length << " rkey=" << destination.rkey << '\n';
    if (config.qp.submission_mode == nds::NpuRaSubmissionMode::Aicpu) {
        if (!control.receive_memory_descriptor(aicpu_sync, &control_error) ||
            aicpu_sync.flags != NDS_MEMORY_DESCRIPTOR_FLAG_AICPU_SYNC || aicpu_sync.length < 24U ||
            (aicpu_sync.access_flags & NDS_MEMORY_ACCESS_REMOTE_WRITE) == 0U || aicpu_sync.rkey == 0U) {
            std::cerr << "CPU AICPU sync-MR exchange failed or is incompatible: " << control_error << '\n';
            return EXIT_FAILURE;
        }
    }
    std::vector<unsigned char> payload(static_cast<std::size_t>(destination.length));
    for (std::size_t index = 0; index < payload.size(); ++index) payload[index] = static_cast<unsigned char>(index ^ 0x5aU);
    if (!context.allocate_device_memory(payload.size(), &device_buffer) ||
        !context.copy_host_to_device(device_buffer, payload.data(), payload.size()) ||
        !qp.register_memory(device_buffer, destination.length, NDS_RA_ACCESS_DIRECT_NPU, source_mr, &mr_handle)) {
        std::cerr << "NPU source preparation failed: " << (qp.error().empty() ? context.error() : qp.error()) << '\n';
        goto out;
    }
    std::cout << "NPU source MR: addr=" << device_buffer << " bytes=" << destination.length
              << " lkey=" << source_mr.local_key << " rkey=" << source_mr.remote_key << '\n';
    if (!print_qp_status(qp, "before RDMA Write") || !print_port_status(qp, "before RDMA Write")) goto out;
    if (config.qp.submission_mode == nds::NpuRaSubmissionMode::HostRa) {
        if (!qp.post_rdma_write({reinterpret_cast<std::uint64_t>(device_buffer), static_cast<std::uint32_t>(destination.length), source_mr.local_key},
                                destination.address, destination.rkey, true, response)) {
            std::cerr << "RaTypicalSendWr(RDMA Write) failed: " << qp.error() << '\n'; goto out;
        }
        std::cout << "Posted one signaled RDMA Write: doorbell_index=" << response.doorbell.db_index
                  << " doorbell_info=0x" << std::hex << response.doorbell.db_info << std::dec << '\n';
        if (!context.submit_rdma_doorbell(response.doorbell.db_index,
                                          static_cast<std::uint64_t>(response.doorbell.db_info))) {
            std::cerr << "rtRDMADBSend failed after RaTypicalSendWr: " << context.error() << '\n';
            goto out;
        }
        std::cout << "Submitted OPBASE RDMA doorbell on the runtime default stream.\n";
        ok = wait_for_send_completion(qp);
        if (!ok) print_failure_diagnostics(qp);
    } else {
        if (!qp.has_aicpu_qp_info() || !context.allocate_device_memory(24U, &local_sync_buffer) ||
            !context.zero_device_memory(local_sync_buffer, 24U) ||
            !qp.register_memory(local_sync_buffer, 24U, NDS_RA_ACCESS_DIRECT_NPU, local_sync_mr, &local_sync_mr_handle) ||
            !aicpu_launcher.load(context.acl_api(), config.aicpu_kernel_config)) {
            std::cerr << "AICPU RoCE Tx setup failed: "
                      << (aicpu_launcher.error().empty() ? (qp.error().empty() ? context.error() : qp.error()) : aicpu_launcher.error())
                      << '\n';
            goto out;
        }
        const nds_ra_ai_qp_info &ai_qp = qp.aicpu_qp_info();
        const nds::AicpuRoceTxRequest request{
            source_mr.local_key, destination.rkey,
            {ai_qp.ai_qp_address, ai_qp.sq_index, ai_qp.db_index, static_cast<std::uint16_t>(config.qp.retry_count),
             static_cast<std::uint16_t>(config.qp.retry_timeout)},
            destination.address, reinterpret_cast<std::uint64_t>(device_buffer), destination.length,
            reinterpret_cast<std::uint64_t>(local_sync_buffer), aicpu_sync.address, local_sync_mr.local_key,
            aicpu_sync.rkey};
        ok = aicpu_launcher.launch_and_wait(request, static_cast<std::int32_t>(kCompletionTimeoutMs));
        if (!ok) std::cerr << "RunTransportRoceTx failed: " << aicpu_launcher.error() << '\n';
    }
out:
    aicpu_launcher.reset();
    if (local_sync_mr_handle != nullptr && !qp.deregister_memory(local_sync_mr_handle))
        std::cerr << "RaDeregisterMr(AICPU sync) cleanup failed: " << qp.error() << '\n';
    if (local_sync_buffer != nullptr && !context.free_device_memory(local_sync_buffer))
        std::cerr << "aclrtFree(AICPU sync) cleanup failed: " << context.error() << '\n';
    if (mr_handle != nullptr && !qp.deregister_memory(mr_handle)) std::cerr << "RaDeregisterMr cleanup failed: " << qp.error() << '\n';
    if (device_buffer != nullptr && !context.free_device_memory(device_buffer)) std::cerr << "aclrtFree cleanup failed: " << context.error() << '\n';
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}

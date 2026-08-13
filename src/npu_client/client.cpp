#include "nds/aicpu_roce.hpp"
#include "nds/aiv_roce.hpp"
#include "nds/control_plane.hpp"
#include "nds/host_ra.hpp"
#include "nds/npu_ra_context.hpp"
#include "nds/npu_ra_qp.hpp"

#include <arpa/inet.h>

#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

constexpr std::uint64_t kMaxTransferBytes = 64U * 1024U;
constexpr std::uint32_t kCompletionTimeoutMs = 5000U;
constexpr std::uint32_t kCqeErrorCapacity = 128U;
constexpr std::uint32_t kMaxQpOnlyHoldMs = 60000U;
constexpr std::uint32_t kMaxAivWriteCount = 16U;

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
    std::string aiv_kernel;
    std::uint32_t aiv_write_count{1U};
    std::uint32_t aiv_launch_count{1U};
};

class DeviceAllocation {
public:
    explicit DeviceAllocation(nds::NpuRaContext &context) : context_(context) {}
    ~DeviceAllocation()
    {
        if (address_ != nullptr && !context_.free_device_memory(address_)) {
            std::cerr << "aclrtFree cleanup failed: " << context_.error() << '\n';
        }
    }
    DeviceAllocation(const DeviceAllocation &) = delete;
    DeviceAllocation &operator=(const DeviceAllocation &) = delete;

    bool allocate(std::size_t size)
    {
        if (address_ != nullptr) return false;
        return context_.allocate_device_memory(size, &address_);
    }
    void *get() const noexcept { return address_; }

private:
    nds::NpuRaContext &context_;
    void *address_{};
};

class RegisteredMemory {
public:
    explicit RegisteredMemory(nds::NpuRaQp &qp) : qp_(qp) {}
    ~RegisteredMemory()
    {
        if (handle_ != nullptr && !qp_.deregister_memory(handle_)) {
            std::cerr << "RaDeregisterMr cleanup failed: " << qp_.error() << '\n';
        }
    }
    RegisteredMemory(const RegisteredMemory &) = delete;
    RegisteredMemory &operator=(const RegisteredMemory &) = delete;

    bool register_memory(void *address, std::uint64_t size, int access)
    {
        return qp_.register_memory(address, size, access, info_, &handle_);
    }
    const nds_ra_mr_info &info() const noexcept { return info_; }

private:
    nds::NpuRaQp &qp_;
    nds_ra_mr_info info_{};
    void *handle_{};
};

void usage(const char *program)
{
    std::cerr
        << "usage: " << program << " --ascendcl ABS_PATH --runtime ABS_PATH --ra ABS_PATH"
        << " --npu-ip IPV4 --logical-device ID --physical-device ID --cpu-ip IPV4 --execute"
        << " [--submission-mode host-ra|aicpu|aiv] [--aicpu-kernel-config ABS_PATH] [--aiv-kernel ABS_PATH]"
        << " [--aiv-write-count COUNT] [--aiv-launch-count COUNT]"
        << " [--qp-only] [--qp-only-hold-ms MS] [--port PORT] [--path-mtu BYTES] [--tcp-port PORT] [--tcp-timeout-ms MS]\n\n"
        << "Creates one NPU0 RA RC QP and exchanges QP metadata with one CPU verbs server. By default it"
        << " receives one bounded CPU destination-MR descriptor and submits exactly one RDMA Write;"
        << " host-ra uses RaTypicalSendWr/rtRDMADBSend/RaPollCq; aicpu launches NDS's standard-CP1 post primitive;"
        << " aiv launches NDS's AIV binary with a copied AI-SQ descriptor, posts one signaled Write WQE, and rings its hardware doorbell."
        << " --qp-only validates QP establishment and posts no memory registration or work request;"
        << " --qp-only-hold-ms keeps that QP alive briefly for passive diagnostics."
        << " HCOMM/HCCL bootstrap and rank tables are intentionally not used.\n";
}

bool parse_u32(const char *text, std::uint32_t *value)
{
    if (text == nullptr || value == nullptr) return false;
    const char *const end = text + std::char_traits<char>::length(text);
    const auto [parsed_end, error] = std::from_chars(text, end, *value);
    return error == std::errc{} && parsed_end == end;
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
            const std::string_view mode(text);
            if (mode == "host-ra") config->qp.submission_mode = nds::NpuRaSubmissionMode::HostRa;
            else if (mode == "aicpu") config->qp.submission_mode = nds::NpuRaSubmissionMode::Aicpu;
            else if (mode == "aiv") config->qp.submission_mode = nds::NpuRaSubmissionMode::Aiv;
            else return false;
        } else if (argument == "--aicpu-kernel-config") {
            const char *text = value(); if (text == nullptr || text[0] == '\0') return false;
            config->aicpu_kernel_config = text;
        } else if (argument == "--aiv-kernel") {
            const char *text = value(); if (text == nullptr || text[0] == '\0') return false;
            config->aiv_kernel = text;
        } else if (argument == "--aiv-write-count") {
            const char *text = value();
            if (text == nullptr || !parse_u32(text, &config->aiv_write_count) ||
                config->aiv_write_count == 0U || config->aiv_write_count > kMaxAivWriteCount) return false;
        } else if (argument == "--aiv-launch-count") {
            const char *text = value();
            if (text == nullptr || !parse_u32(text, &config->aiv_launch_count) ||
                config->aiv_launch_count == 0U || config->aiv_launch_count > kMaxAivWriteCount) return false;
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
           (config->qp.submission_mode != nds::NpuRaSubmissionMode::Aicpu || !config->aicpu_kernel_config.empty()) &&
           (config->qp.submission_mode != nds::NpuRaSubmissionMode::Aiv || !config->aiv_kernel.empty()) &&
           (config->qp.submission_mode == nds::NpuRaSubmissionMode::Aiv || config->aiv_write_count == 1U) &&
           (config->qp.submission_mode == nds::NpuRaSubmissionMode::Aiv || config->aiv_launch_count == 1U) &&
           (config->qp.submission_mode != nds::NpuRaSubmissionMode::Aiv ||
            config->aiv_write_count <= kMaxAivWriteCount / config->aiv_launch_count);
}

std::string format_gid(const std::uint8_t gid[NDS_GID_BYTES])
{
    std::array<char, INET6_ADDRSTRLEN> text{};
    if (inet_ntop(AF_INET6, gid, text.data(), text.size()) == nullptr) {
        return "<invalid>";
    }
    return text.data();
}

void print_endpoint(const char *label, const nds_rc_endpoint &endpoint)
{
    const std::string gid = format_gid(endpoint.gid);
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

bool wait_for_send_completions(nds::NpuRaQp &qp, std::uint32_t expected_count)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kCompletionTimeoutMs);
    nds_ra_completion completion{};
    std::uint32_t completed = 0U;
    if (expected_count == 0U) return false;
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
            if (completion.status != NDS_RA_WC_SUCCESS) return false;
            ++completed;
            if (completed == expected_count) return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::cerr << "timed out waiting " << kCompletionTimeoutMs << " ms for " << expected_count
              << " signaled NPU RDMA Write completions; received " << completed << '\n';
    return false;
}

} // namespace

int main(int argc, char **argv)
{
    ClientConfig config;
    nds::NpuRaContext context;
    nds::NpuRaQp qp;
    nds::TcpControlPlane control;
    nds_rc_endpoint local{};
    nds_memory_descriptor destination{};
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
    std::vector<unsigned char> payload(static_cast<std::size_t>(destination.length));
    for (std::size_t index = 0; index < payload.size(); ++index) payload[index] = static_cast<unsigned char>(index ^ 0x5aU);
    DeviceAllocation device_buffer(context);
    RegisteredMemory source_mr(qp);
    DeviceAllocation aiv_request_buffer(context);
    nds::AicpuRdmaPostLauncher aicpu_launcher;
    nds::AivRdmaWriteLauncher aiv_launcher;
    if (!device_buffer.allocate(payload.size()) ||
        !context.copy_host_to_device(device_buffer.get(), payload.data(), payload.size()) ||
        !source_mr.register_memory(device_buffer.get(), destination.length, NDS_RA_ACCESS_DIRECT_NPU)) {
        std::cerr << "NPU source preparation failed: " << (qp.error().empty() ? context.error() : qp.error()) << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "NPU source MR: addr=" << device_buffer.get() << " bytes=" << destination.length
              << " lkey=" << source_mr.info().local_key << " rkey=" << source_mr.info().remote_key << '\n';
    if (!print_qp_status(qp, "before RDMA Write") || !print_port_status(qp, "before RDMA Write")) return EXIT_FAILURE;
    if (config.qp.submission_mode == nds::NpuRaSubmissionMode::HostRa) {
        std::string submission_error;
        const nds::HostRaWriteRequest request{
            {reinterpret_cast<std::uint64_t>(device_buffer.get()), static_cast<std::uint32_t>(destination.length),
             source_mr.info().local_key},
            destination.address,
            destination.rkey};
        if (!nds::submit_host_ra_write(context, qp, request, submission_error)) {
            std::cerr << "host RA RDMA Write submission failed: " << submission_error << '\n';
            return EXIT_FAILURE;
        }
        if (!wait_for_send_completions(qp, 1U)) {
            print_failure_diagnostics(qp);
            return EXIT_FAILURE;
        }
    } else if (config.qp.submission_mode == nds::NpuRaSubmissionMode::Aicpu) {
        if (!qp.has_aicpu_qp_info() || !aicpu_launcher.load(context.acl_api(), config.aicpu_kernel_config)) {
            std::cerr << "NDS AICPU RDMA-post setup failed: "
                      << (aicpu_launcher.error().empty() ? (qp.error().empty() ? context.error() : qp.error()) : aicpu_launcher.error())
                      << '\n';
            return EXIT_FAILURE;
        }
        const nds_ra_ai_qp_info &ai_qp = qp.aicpu_qp_info();
        const nds::AicpuRdmaPostRequest request{
            NDS_AICPU_RDMA_WRITE, ai_qp.ai_qp_address, source_mr.info().local_key, destination.rkey,
            reinterpret_cast<std::uint64_t>(device_buffer.get()), destination.address, destination.length, 1U,
            static_cast<std::uint32_t>(config.context.logical_device_id)};
        if (!aicpu_launcher.launch_and_wait(request, static_cast<std::int32_t>(kCompletionTimeoutMs))) {
            std::cerr << "NdsAicpuRdmaPost failed: " << aicpu_launcher.error() << '\n';
            return EXIT_FAILURE;
        }
    } else {
        if (!qp.has_aicpu_qp_info()) {
            std::cerr << "NDS AIV resource setup failed: RaAiQpCreate returned no AI-QP metadata\n";
            return EXIT_FAILURE;
        }
        const nds_ra_ai_qp_info &ai_qp = qp.aicpu_qp_info();
        if (ai_qp.data_plane_info == nullptr) {
            std::cerr << "NDS AIV resource setup failed: RaAiQpCreate returned no AI data-plane metadata\n";
            return EXIT_FAILURE;
        }
        const auto *data_plane = reinterpret_cast<const nds_ra_ai_data_plane_info *>(ai_qp.data_plane_info);
        nds_aiv_rdma_write_request_v1 device_request{};
        const nds::AivRdmaWriteRequest request{
            data_plane->send_wq, config.qp.service_level, source_mr.info().local_key, destination.rkey,
            reinterpret_cast<std::uint64_t>(device_buffer.get()), destination.address,
            static_cast<std::uint32_t>(destination.length), config.aiv_write_count};
        std::cout << "NPU AIV SQ: wqn=" << data_plane->send_wq.wqn
                  << " buf=0x" << std::hex << data_plane->send_wq.buffer_address
                  << " wqebb=" << std::dec << data_plane->send_wq.wqebb_size
                  << " depth=" << data_plane->send_wq.depth
                  << " head=0x" << std::hex << data_plane->send_wq.head_address
                  << " tail=0x" << data_plane->send_wq.tail_address
                  << " swdb=0x" << data_plane->send_wq.software_doorbell_address
                  << " dbreg=0x" << data_plane->send_wq.doorbell_register_address << std::dec << '\n';
        if (!aiv_launcher.load(context.acl_api(), config.aiv_kernel)) {
            std::cerr << "NDS AIV binary setup failed: " << aiv_launcher.error() << '\n';
            return EXIT_FAILURE;
        }
        if (!aiv_launcher.make_device_request(request, &device_request)) {
            std::cerr << "NDS AIV request setup failed: " << aiv_launcher.error() << '\n';
            return EXIT_FAILURE;
        }
        if (!aiv_request_buffer.allocate(sizeof(device_request))) {
            std::cerr << "NDS AIV request allocation failed: " << context.error() << '\n';
            return EXIT_FAILURE;
        }
        if (!context.copy_host_to_device(aiv_request_buffer.get(), &device_request, sizeof(device_request))) {
            std::cerr << "NDS AIV request copy failed: " << context.error() << '\n';
            return EXIT_FAILURE;
        }
        for (std::uint32_t launch = 0U; launch < config.aiv_launch_count; ++launch) {
            if (!aiv_launcher.launch_write_and_wait(reinterpret_cast<std::uint64_t>(aiv_request_buffer.get()),
                                                    static_cast<std::int32_t>(kCompletionTimeoutMs))) {
                std::cerr << "NdsAivRdmaWrite launch " << (launch + 1U) << "/" << config.aiv_launch_count
                          << " failed: " << aiv_launcher.error() << '\n';
                return EXIT_FAILURE;
            }
        }
        std::cout << "NDS AIV completed " << config.aiv_launch_count << " kernel launches and posted "
                  << (config.aiv_launch_count * config.aiv_write_count)
                  << " signaled RDMA Writes through the AI SQ hardware doorbell.\n";
        std::cout << "HCCP owns this AI QP's CQ and processes its completion channel asynchronously.\n";
    }
    const nds_transfer_status submitted{NDS_TRANSFER_SUBMITTED, destination.transaction_id};
    nds_transfer_status verified{};
    if (!control.send_transfer_status(submitted, &control_error) ||
        !control.receive_transfer_status(verified, &control_error)) {
        std::cerr << "CPU transfer acknowledgment failed: " << control_error << '\n';
        return EXIT_FAILURE;
    }
    if (verified.transaction_id != destination.transaction_id || verified.status != NDS_TRANSFER_VERIFIED) {
        std::cerr << "CPU rejected RDMA transfer validation\n";
        return EXIT_FAILURE;
    }
    std::cout << "CPU verified the RDMA transfer; NPU resources may now be released.\n";
    return EXIT_SUCCESS;
}

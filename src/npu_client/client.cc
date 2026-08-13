#include "nds/aicpu_roce.hh"
#include "nds/aiv_roce.hh"
#include "nds/peer_exchange.hh"
#include "nds/host_ra.hh"
#include "nds/logging.hh"
#include "nds/npu_ra_context.hh"
#include "nds/npu_ra_qp.hh"

#include <CLI/CLI.hpp>

#include <arpa/inet.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>
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
    std::string log_sink{"stderr"};
    std::string log_level{"info"};
};

class DeviceAllocation {
public:
    explicit DeviceAllocation(nds::NpuRaContext &context) : context_(context) {}
    ~DeviceAllocation()
    {
        if (address_ != nullptr && !context_.free_device_memory(address_)) {
            NDS_LOG_ERROR("npu-client", "aclrtFree cleanup failed: {}", context_.error());
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
            NDS_LOG_ERROR("npu-client", "RaDeregisterMr cleanup failed: {}", qp_.error());
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

int parse_args(int argc, char **argv, ClientConfig *config, std::string &error, bool &exit_requested)
{
    CLI::App app{"Create one NPU RA RC QP and submit an NPU-to-CPU RDMA Write."};
    app.add_flag("--execute", config->execute, "Permit QP creation and submission")->required();
    app.add_flag("--qp-only", config->qp_only, "Validate QP establishment only");
    app.add_option("--qp-only-hold-ms", config->qp_only_hold_ms, "Passive diagnostic hold time")
        ->check(CLI::Range(std::uint32_t{0}, kMaxQpOnlyHoldMs));
    app.add_option("--ascendcl", config->context.ascendcl_library, "AscendCL shared library")->required();
    app.add_option("--runtime", config->context.runtime_library, "CANN runtime shared library")->required();
    app.add_option("--ra", config->context.ra_library, "CANN RA shared library")->required();
    std::string submission_mode{"host-ra"};
    app.add_option("--submission-mode", submission_mode, "Submission implementation")
        ->check(CLI::IsMember({"host-ra", "aicpu", "aiv"}));
    app.add_option("--aicpu-kernel-config", config->aicpu_kernel_config, "AICPU kernel package configuration");
    app.add_option("--aiv-kernel", config->aiv_kernel, "AIV kernel binary");
    app.add_option("--aiv-write-count", config->aiv_write_count, "Writes per AIV launch")
        ->check(CLI::Range(std::uint32_t{1}, kMaxAivWriteCount));
    app.add_option("--aiv-launch-count", config->aiv_launch_count, "AIV launches")
        ->check(CLI::Range(std::uint32_t{1}, kMaxAivWriteCount));
    app.add_option("--npu-ip", config->qp.local_ipv4, "NPU RoCE IPv4 address")->required();
    app.add_option("--logical-device", config->context.logical_device_id, "NPU logical device")->required();
    app.add_option("--physical-device", config->context.physical_device_id, "NPU physical device")->required();
    app.add_option("--port", config->qp.port_num, "NPU RoCE port")
        ->check(CLI::Range(std::uint16_t{1}, std::numeric_limits<std::uint16_t>::max()));
    app.add_option("--path-mtu", config->qp.path_mtu, "Path MTU")
        ->check(CLI::Range(std::uint16_t{1}, std::numeric_limits<std::uint16_t>::max()));
    app.add_option("--cpu-ip", config->cpu_ipv4, "CPU peer IPv4 address")->required();
    app.add_option("--tcp-port", config->tcp_port, "TCP peer-exchange port")
        ->check(CLI::Range(std::uint16_t{1}, std::numeric_limits<std::uint16_t>::max()));
    app.add_option("--tcp-timeout-ms", config->tcp_timeout_ms, "TCP peer-exchange timeout")->check(CLI::PositiveNumber);
    app.add_option("--log-sink", config->log_sink, "Log sink")->check(CLI::IsMember({"stderr", "stdout", "syslog", "none"}));
    app.add_option("--log-level", config->log_level, "Log level")
        ->check(CLI::IsMember({"trace", "debug", "info", "warn", "error", "critical", "off"}));
    try {
        app.parse(argc, argv);
    } catch (const CLI::CallForHelp &help) {
        exit_requested = true;
        return app.exit(help);
    } catch (const CLI::ParseError &parse_error) {
        return app.exit(parse_error);
    }
    config->qp.physical_device_id = config->context.physical_device_id;
    if (submission_mode == "aicpu") config->qp.submission_mode = nds::NpuRaSubmissionMode::Aicpu;
    if (submission_mode == "aiv") config->qp.submission_mode = nds::NpuRaSubmissionMode::Aiv;
    if (!(config->execute && (config->qp_only || config->qp_only_hold_ms == 0U) &&
           !config->context.ascendcl_library.empty() && !config->context.runtime_library.empty() &&
           !config->context.ra_library.empty() && !config->qp.local_ipv4.empty() && !config->cpu_ipv4.empty() &&
           (config->qp.submission_mode != nds::NpuRaSubmissionMode::Aicpu || !config->aicpu_kernel_config.empty()) &&
           (config->qp.submission_mode != nds::NpuRaSubmissionMode::Aiv || !config->aiv_kernel.empty()) &&
           (config->qp.submission_mode == nds::NpuRaSubmissionMode::Aiv || config->aiv_write_count == 1U) &&
           (config->qp.submission_mode == nds::NpuRaSubmissionMode::Aiv || config->aiv_launch_count == 1U) &&
           (config->qp.submission_mode != nds::NpuRaSubmissionMode::Aiv ||
            config->aiv_write_count <= kMaxAivWriteCount / config->aiv_launch_count))) {
        error = "invalid option combination";
        return -1;
    }
    return 0;
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
    NDS_LOG_INFO_LINE("npu-client") << label << " endpoint: qpn=" << endpoint.qp_num << " psn=" << endpoint.psn
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
        NDS_LOG_ERROR_LINE("npu-client") << "RaRdevGetPortStatus(" << stage << ") failed: " << qp.error() << '\n';
        return false;
    }
    NDS_LOG_INFO_LINE("npu-client") << "NPU rdev port status " << stage << ": " << port_status_name(status) << " (" << status << ")\n";
    if (status != NDS_RA_PORT_STATUS_ACTIVE) {
        NDS_LOG_ERROR_LINE("npu-client") << "NPU rdev port is not active at " << stage << "; refusing to continue toward the RDMA Write\n";
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
        NDS_LOG_ERROR_LINE("npu-client") << "RaRdevGetSupportLite failed: " << qp.error() << '\n';
        return false;
    }
    NDS_LOG_INFO_LINE("npu-client") << "NPU rdev RDMA-lite support: " << lite_support_name(support_lite)
              << " (" << support_lite << ")\n";
    if (support_lite == NDS_RA_LITE_NOT_SUPPORTED) {
        NDS_LOG_ERROR_LINE("npu-client") << "NPU rdev does not support the required RDMA-lite send path; refusing to continue\n";
        return false;
    }
    return true;
}

bool print_qp_status(nds::NpuRaQp &qp, const char *stage)
{
    int status = -1;
    if (!qp.query_status(status)) {
        NDS_LOG_ERROR_LINE("npu-client") << "RaGetQpStatus(" << stage << ") failed: " << qp.error() << '\n';
        return false;
    }
    NDS_LOG_INFO_LINE("npu-client") << "NPU QP status " << stage << ": " << qp_status_name(status) << " (" << status << ")\n";
    if (status != NDS_RA_QP_STATUS_CONNECTED) {
        NDS_LOG_ERROR_LINE("npu-client") << "NPU QP is not connected at " << stage << "; refusing to post the RDMA Write\n";
        return false;
    }
    return true;
}

void print_failure_diagnostics(nds::NpuRaQp &qp)
{
    int status = -1;
    if (qp.query_status(status)) {
        NDS_LOG_ERROR_LINE("npu-client") << "NPU QP status after write failure: " << qp_status_name(status) << " (" << status << ")\n";
    } else {
        NDS_LOG_ERROR_LINE("npu-client") << "RaGetQpStatus(after write failure) failed: " << qp.error() << '\n';
    }
    if (qp.query_port_status(status)) {
        NDS_LOG_ERROR_LINE("npu-client") << "NPU rdev port status after write failure: " << port_status_name(status) << " (" << status << ")\n";
    } else {
        NDS_LOG_ERROR_LINE("npu-client") << "RaRdevGetPortStatus(after write failure) failed: " << qp.error() << '\n';
    }

    std::array<nds_ra_cqe_error, kCqeErrorCapacity> errors{};
    std::uint32_t count = kCqeErrorCapacity;
    if (!qp.query_cqe_errors(errors.data(), count)) {
        NDS_LOG_ERROR_LINE("npu-client") << "RaRdevGetCqeErrInfoList(after write failure) failed: " << qp.error() << '\n';
        return;
    }
    NDS_LOG_ERROR_LINE("npu-client") << "NPU CQE error records consumed after write failure: " << count << '\n';
    for (std::uint32_t index = 0; index < count; ++index) {
        const nds_ra_cqe_error &error = errors[index];
        NDS_LOG_ERROR_LINE("npu-client") << "  cqe_error[" << index << "]: status=" << error.status << " qpn=" << error.qp_number
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
            NDS_LOG_ERROR_LINE("npu-client") << "RaPollCq(send) failed: " << qp.error() << '\n';
            return false;
        }
        if (count > 0) {
            NDS_LOG_INFO_LINE("npu-client") << "NPU send completion: status=" << completion_status_name(completion.status)
                      << " (" << completion.status << ") opcode=" << completion.opcode
                      << " qpn=" << completion.qp_number << " bytes=" << completion.byte_length
                      << " vendor_error=" << completion.vendor_error << '\n';
            if (completion.status != NDS_RA_WC_SUCCESS) return false;
            ++completed;
            if (completed == expected_count) return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    NDS_LOG_ERROR_LINE("npu-client") << "timed out waiting " << kCompletionTimeoutMs << " ms for " << expected_count
              << " signaled NPU RDMA Write completions; received " << completed << '\n';
    return false;
}

} // namespace

int main(int argc, char **argv)
{
    std::string log_error;
    (void)nds::log::configure("npu-client", "stderr", "info", log_error);
    ClientConfig config;
    nds::NpuRaContext context;
    nds::NpuRaQp qp;
    nds::TcpPeerExchange peer_exchange;
    nds_rc_endpoint local{};
    nds_memory_descriptor destination{};
    std::string peer_exchange_error;

    bool exit_requested = false;
    const int parse_result = parse_args(argc, argv, &config, log_error, exit_requested);
    if (exit_requested || parse_result != 0) {
        if (parse_result < 0) NDS_LOG_ERROR("npu-client", "{}", log_error);
        return parse_result < 0 ? EXIT_FAILURE : parse_result;
    }
    if (!nds::log::configure("npu-client", config.log_sink, config.log_level, log_error)) {
        NDS_LOG_ERROR("npu-client", "invalid logger configuration: {}", log_error);
        return EXIT_FAILURE;
    }
    if (!context.initialize(config.context)) { NDS_LOG_ERROR_LINE("npu-client") << "direct NPU RA context initialization failed: " << context.error() << '\n'; return EXIT_FAILURE; }
    NDS_LOG_INFO_LINE("npu-client") << "Direct NPU RA context initialized: logical_device=" << config.context.logical_device_id
              << " physical_device=" << config.context.physical_device_id << '\n';
    if (!qp.create(context.ra_api(), config.qp) || !qp.make_qp_only_endpoint(local)) {
        NDS_LOG_ERROR_LINE("npu-client") << "NPU RA QP setup failed: " << qp.error() << '\n'; return EXIT_FAILURE;
    }
    print_endpoint("NPU local", local);
    if (!print_port_status(qp, "after rdev/QP create") || !print_lite_support(qp)) return EXIT_FAILURE;
    if (!nds::TcpPeerExchange::connect(config.cpu_ipv4, config.tcp_port, config.tcp_timeout_ms, peer_exchange, &peer_exchange_error)) {
        NDS_LOG_ERROR_LINE("npu-client") << "TCP peer exchange connect failed: " << peer_exchange_error << '\n'; return EXIT_FAILURE;
    }
    const nds::PeerExchangeResult exchanged = peer_exchange.exchange_as_client(local);
    if (!exchanged.ok || (exchanged.peer.flags & NDS_ENDPOINT_FLAG_QP_ONLY) == 0U) {
        NDS_LOG_ERROR_LINE("npu-client") << "QP endpoint exchange failed: " << exchanged.error << '\n'; return EXIT_FAILURE;
    }
    print_endpoint("CPU remote", exchanged.peer);
    if (!qp.connect(exchanged.peer)) { NDS_LOG_ERROR_LINE("npu-client") << "RaTypicalQpModify failed: " << qp.error() << '\n'; return EXIT_FAILURE; }
    NDS_LOG_INFO_LINE("npu-client") << "RaTypicalQpModify succeeded.\n";
    if (!print_qp_status(qp, "after modify") || !print_port_status(qp, "after modify")) return EXIT_FAILURE;
    if (config.qp_only) {
        if (config.qp_only_hold_ms != 0U) {
            NDS_LOG_INFO_LINE("npu-client") << "Holding the QP-only connection for " << config.qp_only_hold_ms
                      << " ms for passive diagnostics; no memory registration or RDMA work request will be posted.\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(config.qp_only_hold_ms));
        }
        NDS_LOG_INFO_LINE("npu-client") << "QP-only validation passed; no memory registration or RDMA work request was posted.\n";
        return EXIT_SUCCESS;
    }
    if (!peer_exchange.receive_memory_descriptor(destination, &peer_exchange_error)) {
        NDS_LOG_ERROR_LINE("npu-client") << "CPU destination-MR exchange failed: " << peer_exchange_error << '\n'; return EXIT_FAILURE;
    }
    if ((destination.access_flags & NDS_MEMORY_ACCESS_REMOTE_WRITE) == 0U || destination.length == 0U ||
        destination.length > kMaxTransferBytes || destination.length > std::numeric_limits<std::uint32_t>::max()) {
        NDS_LOG_ERROR_LINE("npu-client") << "CPU advertised an unsupported destination-MR descriptor\n"; return EXIT_FAILURE;
    }
    NDS_LOG_INFO_LINE("npu-client") << "CPU destination MR: transaction=" << destination.transaction_id << " addr=0x" << std::hex
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
        NDS_LOG_ERROR_LINE("npu-client") << "NPU source preparation failed: " << (qp.error().empty() ? context.error() : qp.error()) << '\n';
        return EXIT_FAILURE;
    }
    NDS_LOG_INFO_LINE("npu-client") << "NPU source MR: addr=" << device_buffer.get() << " bytes=" << destination.length
              << " lkey=" << source_mr.info().local_key << " rkey=" << source_mr.info().remote_key << '\n';
    if (!print_qp_status(qp, "before RDMA Write") || !print_port_status(qp, "before RDMA Write")) return EXIT_FAILURE;
    if (config.qp.submission_mode == nds::NpuRaSubmissionMode::HostRa) {
        std::string submission_error;
        const nds::HostRaPostRequest request{
            {reinterpret_cast<std::uint64_t>(device_buffer.get()), static_cast<std::uint32_t>(destination.length),
             source_mr.info().local_key},
            NDS_RA_WR_RDMA_WRITE,
            destination.address,
            destination.rkey};
        if (!nds::submit_host_ra(context, qp, request, submission_error)) {
            NDS_LOG_ERROR_LINE("npu-client") << "host RA RDMA Write submission failed: " << submission_error << '\n';
            return EXIT_FAILURE;
        }
        if (!wait_for_send_completions(qp, 1U)) {
            print_failure_diagnostics(qp);
            return EXIT_FAILURE;
        }
    } else if (config.qp.submission_mode == nds::NpuRaSubmissionMode::Aicpu) {
        if (!qp.has_aicpu_qp_info() || !aicpu_launcher.load(context.acl_api(), config.aicpu_kernel_config)) {
            NDS_LOG_ERROR_LINE("npu-client") << "NDS AICPU RDMA-post setup failed: "
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
            NDS_LOG_ERROR_LINE("npu-client") << "NdsAicpuRdmaPost failed: " << aicpu_launcher.error() << '\n';
            return EXIT_FAILURE;
        }
    } else {
        if (!qp.has_aicpu_qp_info()) {
            NDS_LOG_ERROR_LINE("npu-client") << "NDS AIV resource setup failed: RaAiQpCreate returned no AI-QP metadata\n";
            return EXIT_FAILURE;
        }
        const nds_ra_ai_qp_info &ai_qp = qp.aicpu_qp_info();
        if (ai_qp.data_plane_info == nullptr) {
            NDS_LOG_ERROR_LINE("npu-client") << "NDS AIV resource setup failed: RaAiQpCreate returned no AI data-plane metadata\n";
            return EXIT_FAILURE;
        }
        const auto *data_plane = reinterpret_cast<const nds_ra_ai_data_plane_info *>(ai_qp.data_plane_info);
        nds_aiv_rdma_write_request device_request{};
        const nds::AivRdmaWriteRequest request{
            data_plane->send_wq, config.qp.service_level, source_mr.info().local_key, destination.rkey,
            reinterpret_cast<std::uint64_t>(device_buffer.get()), destination.address,
            static_cast<std::uint32_t>(destination.length), config.aiv_write_count};
        NDS_LOG_INFO_LINE("npu-client") << "NPU AIV SQ: wqn=" << data_plane->send_wq.wqn
                  << " buf=0x" << std::hex << data_plane->send_wq.buffer_address
                  << " wqebb=" << std::dec << data_plane->send_wq.wqebb_size
                  << " depth=" << data_plane->send_wq.depth
                  << " head=0x" << std::hex << data_plane->send_wq.head_address
                  << " tail=0x" << data_plane->send_wq.tail_address
                  << " swdb=0x" << data_plane->send_wq.software_doorbell_address
                  << " dbreg=0x" << data_plane->send_wq.doorbell_register_address << std::dec << '\n';
        if (!aiv_launcher.load(context.acl_api(), config.aiv_kernel)) {
            NDS_LOG_ERROR_LINE("npu-client") << "NDS AIV binary setup failed: " << aiv_launcher.error() << '\n';
            return EXIT_FAILURE;
        }
        if (!aiv_launcher.make_device_request(request, &device_request)) {
            NDS_LOG_ERROR_LINE("npu-client") << "NDS AIV request setup failed: " << aiv_launcher.error() << '\n';
            return EXIT_FAILURE;
        }
        if (!aiv_request_buffer.allocate(sizeof(device_request))) {
            NDS_LOG_ERROR_LINE("npu-client") << "NDS AIV request allocation failed: " << context.error() << '\n';
            return EXIT_FAILURE;
        }
        if (!context.copy_host_to_device(aiv_request_buffer.get(), &device_request, sizeof(device_request))) {
            NDS_LOG_ERROR_LINE("npu-client") << "NDS AIV request copy failed: " << context.error() << '\n';
            return EXIT_FAILURE;
        }
        for (std::uint32_t launch = 0U; launch < config.aiv_launch_count; ++launch) {
            if (!aiv_launcher.launch_write_and_wait(reinterpret_cast<std::uint64_t>(aiv_request_buffer.get()),
                                                    static_cast<std::int32_t>(kCompletionTimeoutMs))) {
                NDS_LOG_ERROR_LINE("npu-client") << "NdsAivRdmaWrite launch " << (launch + 1U) << "/" << config.aiv_launch_count
                          << " failed: " << aiv_launcher.error() << '\n';
                return EXIT_FAILURE;
            }
        }
        NDS_LOG_INFO_LINE("npu-client") << "NDS AIV completed " << config.aiv_launch_count << " kernel launches and posted "
                  << (config.aiv_launch_count * config.aiv_write_count)
                  << " signaled RDMA Writes through the AI SQ hardware doorbell.\n";
        NDS_LOG_INFO_LINE("npu-client") << "HCCP owns this AI QP's CQ and processes its completion channel asynchronously.\n";
    }
    const nds_transfer_status submitted{NDS_TRANSFER_SUBMITTED, destination.transaction_id};
    nds_transfer_status verified{};
    if (!peer_exchange.send_transfer_status(submitted, &peer_exchange_error) ||
        !peer_exchange.receive_transfer_status(verified, &peer_exchange_error)) {
        NDS_LOG_ERROR_LINE("npu-client") << "CPU transfer acknowledgment failed: " << peer_exchange_error << '\n';
        return EXIT_FAILURE;
    }
    if (verified.transaction_id != destination.transaction_id || verified.status != NDS_TRANSFER_VERIFIED) {
        NDS_LOG_ERROR_LINE("npu-client") << "CPU rejected RDMA transfer validation\n";
        return EXIT_FAILURE;
    }
    NDS_LOG_INFO_LINE("npu-client") << "CPU verified the RDMA transfer; NPU resources may now be released.\n";
    return EXIT_SUCCESS;
}

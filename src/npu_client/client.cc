#include "nds/peer_exchange.hh"
#include "nds/logging.hh"
#include "nds/npu_ra_context.hh"
#include "nds/npu_ra_qp.hh"
#include "nds/storage_protocol.h"
#include "storage_submission.hh"

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
    std::string aiv_kernel;
    std::string operation{"write"};
    std::uint64_t offset{};
    std::uint32_t bytes{4096U};
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
    CLI::App app{"Submit one NDS storage command from an NPU to a CPU memory namespace."};
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
    app.add_option("--operation", config->operation, "Storage operation")->check(CLI::IsMember({"read", "write"}));
    app.add_option("--offset", config->offset, "Namespace byte offset");
    app.add_option("--bytes", config->bytes, "Storage transfer length")
        ->check(CLI::Range(std::uint32_t{1}, static_cast<std::uint32_t>(kMaxTransferBytes)));
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
           (config->qp.submission_mode != nds::NpuRaSubmissionMode::Aiv || !config->aiv_kernel.empty()))) {
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

bool wait_for_storage_completion(nds::NpuRaContext &context, const DeviceAllocation &device_completion,
                                 std::uint64_t request_id, std::uint64_t expected_bytes)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kCompletionTimeoutMs);
    nds_storage_completion_wire wire{};
    nds_storage_completion completion{};
    char error[NDS_STORAGE_ERROR_CAPACITY]{};
    while (std::chrono::steady_clock::now() < deadline) {
        if (!context.copy_device_to_host(&wire, device_completion.get(), sizeof(wire))) {
            NDS_LOG_ERROR_LINE("npu-client") << "completion copy failed: " << context.error() << '\n';
            return false;
        }
        if (nds_storage_completion_decode(&wire, &completion, error) != 0) {
            NDS_LOG_ERROR_LINE("npu-client") << "invalid storage completion: " << error << '\n';
            return false;
        }
        if (completion.state == NDS_STORAGE_COMPLETION_COMPLETE) {
            if (completion.request_id != request_id || completion.status != NDS_STORAGE_SUCCESS ||
                completion.bytes_transferred != expected_bytes) {
                NDS_LOG_ERROR_LINE("npu-client") << "storage completion rejected request: request_id="
                          << completion.request_id << " status=" << completion.status
                          << " bytes=" << completion.bytes_transferred << '\n';
                return false;
            }
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    NDS_LOG_ERROR_LINE("npu-client") << "timed out waiting for CPU storage completion record\n";
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
    const std::uint16_t operation = config.operation == "read" ? NDS_STORAGE_READ : NDS_STORAGE_WRITE;
    const std::uint64_t request_id = (static_cast<std::uint64_t>(local.qp_num) << 32U) | local.psn;
    std::vector<unsigned char> payload(config.bytes);
    for (std::size_t index = 0; index < payload.size(); ++index) payload[index] = static_cast<unsigned char>(index ^ 0x5aU);
    DeviceAllocation device_data(context);
    DeviceAllocation device_command(context);
    DeviceAllocation device_completion(context);
    RegisteredMemory data_mr(qp);
    RegisteredMemory command_mr(qp);
    RegisteredMemory completion_mr(qp);
    nds_storage_completion pending{request_id, NDS_STORAGE_COMPLETION_PENDING, NDS_STORAGE_SUCCESS, 0U};
    nds_storage_completion_wire completion_wire{};
    char storage_error[NDS_STORAGE_ERROR_CAPACITY]{};
    if (nds_storage_completion_encode(&pending, &completion_wire, storage_error) != 0 ||
        !device_data.allocate(payload.size()) || !device_command.allocate(sizeof(nds_storage_command_wire)) ||
        !device_completion.allocate(sizeof(completion_wire)) ||
        !context.copy_host_to_device(device_data.get(), payload.data(), payload.size()) ||
        !context.copy_host_to_device(device_completion.get(), &completion_wire, sizeof(completion_wire)) ||
        !data_mr.register_memory(device_data.get(), payload.size(), NDS_RA_ACCESS_DIRECT_NPU) ||
        !command_mr.register_memory(device_command.get(), sizeof(nds_storage_command_wire), NDS_RA_ACCESS_DIRECT_NPU) ||
        !completion_mr.register_memory(device_completion.get(), sizeof(completion_wire), NDS_RA_ACCESS_DIRECT_NPU)) {
        NDS_LOG_ERROR_LINE("npu-client") << "NPU storage buffer preparation failed: "
                  << (qp.error().empty() ? context.error() : qp.error()) << '\n';
        return EXIT_FAILURE;
    }
    nds_storage_bootstrap bootstrap{{reinterpret_cast<std::uint64_t>(device_completion.get()), sizeof(completion_wire),
                                     completion_mr.info().remote_key, NDS_STORAGE_ACCESS_REMOTE_WRITE}};
    nds_storage_namespace storage_namespace{};
    if (!peer_exchange.send_storage_bootstrap(bootstrap, &peer_exchange_error) ||
        !peer_exchange.receive_storage_namespace(storage_namespace, &peer_exchange_error)) {
        NDS_LOG_ERROR_LINE("npu-client") << "storage bootstrap failed: " << peer_exchange_error << '\n';
        return EXIT_FAILURE;
    }
    if (config.offset > storage_namespace.capacity || config.bytes > storage_namespace.capacity - config.offset) {
        NDS_LOG_ERROR_LINE("npu-client") << "requested storage range exceeds CPU namespace capacity\n";
        return EXIT_FAILURE;
    }
    const std::uint32_t data_access = operation == NDS_STORAGE_READ ? NDS_STORAGE_ACCESS_REMOTE_WRITE : NDS_STORAGE_ACCESS_REMOTE_READ;
    nds_storage_command command{request_id, operation, config.offset, config.bytes,
                                {reinterpret_cast<std::uint64_t>(device_data.get()), payload.size(), data_mr.info().remote_key, data_access}};
    nds_storage_command_wire command_wire{};
    if (nds_storage_command_encode(&command, &command_wire, storage_error) != 0 ||
        !context.copy_host_to_device(device_command.get(), &command_wire, sizeof(command_wire))) {
        NDS_LOG_ERROR_LINE("npu-client") << "storage command preparation failed: "
                  << (storage_error[0] == '\0' ? context.error() : storage_error) << '\n';
        return EXIT_FAILURE;
    }
    NDS_LOG_INFO_LINE("npu-client") << "NPU storage command MR: addr=" << device_command.get()
              << " bytes=" << sizeof(command_wire) << " lkey=" << command_mr.info().local_key << '\n';
    if (!print_qp_status(qp, "before storage command") || !print_port_status(qp, "before storage command")) return EXIT_FAILURE;
    std::string submission_error;
    const nds::StorageSubmissionConfig submission_config{config.qp.submission_mode,
        config.context.logical_device_id, config.qp.service_level, config.aicpu_kernel_config, config.aiv_kernel};
    if (!nds::submit_storage_command(context, qp, submission_config,
                                     reinterpret_cast<std::uint64_t>(device_command.get()), sizeof(command_wire),
                                     command_mr.info().local_key, submission_error)) {
        NDS_LOG_ERROR_LINE("npu-client") << "storage command submission failed: " << submission_error << '\n';
        return EXIT_FAILURE;
    }
    if (!wait_for_storage_completion(context, device_completion, request_id, config.bytes)) return EXIT_FAILURE;
    if (operation == NDS_STORAGE_READ) {
        std::vector<unsigned char> result(payload.size());
        if (!context.copy_device_to_host(result.data(), device_data.get(), result.size())) {
            NDS_LOG_ERROR_LINE("npu-client") << "readback copy failed: " << context.error() << '\n';
            return EXIT_FAILURE;
        }
        if (result != std::vector<unsigned char>(result.size(), 0U)) {
            NDS_LOG_ERROR_LINE("npu-client") << "storage Read returned nonzero data from a new namespace\n";
            return EXIT_FAILURE;
        }
    }
    NDS_LOG_INFO_LINE("npu-client") << "CPU completed the NDS storage command; NPU resources may now be released.\n";
    return EXIT_SUCCESS;
}

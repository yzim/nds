#include "launch.hh"

#include "nds/aicpu_launcher.hh"
#include "nds/aiv_launcher.hh"
#include "nds/host_ra.hh"
#include "nds/npu_ra_context.hh"

#include <chrono>
#include <thread>

#include <cstddef>

namespace nds {
using client::RmaConfig;

namespace {

constexpr std::uint32_t kMaxAicpuTransferBytes = 64U * 1024U;

class DeviceAllocation {
public:
    explicit DeviceAllocation(NpuRaContext *context) : context_(context) {}
    ~DeviceAllocation() {
        if (address_ != nullptr && context_ != nullptr)
            (void)context_->free_device_memory(address_);
    }
    DeviceAllocation(const DeviceAllocation &) = delete;
    DeviceAllocation &operator=(const DeviceAllocation &) = delete;

    bool allocate(std::size_t size) {
        return context_ != nullptr && address_ == nullptr && context_->allocate_device_memory(size, &address_);
    }
    void *get() const noexcept {
        return address_;
    }

private:
    NpuRaContext *context_{};
    void *address_{};
};

const char *opcode_name(WorkRequestOpcode opcode) noexcept {
    switch (opcode) {
    case WorkRequestOpcode::Send:
        return "Send";
    case WorkRequestOpcode::RdmaRead:
        return "RDMA Read";
    case WorkRequestOpcode::RdmaWrite:
        return "RDMA Write";
    }
    return "unknown work request";
}

const char *operation_status_name(std::uint32_t status) noexcept {
    switch (status) {
    case NDS_DEVICE_OPERATION_SUCCESS:
        return "success";
    case NDS_DEVICE_OPERATION_INVALID_ARGUMENT:
        return "invalid argument";
    case NDS_DEVICE_OPERATION_SYMBOL_UNAVAILABLE:
        return "provider symbol unavailable";
    case NDS_DEVICE_OPERATION_PROVIDER_FAILED:
        return "provider call failed";
    case NDS_DEVICE_OPERATION_QUEUE_FULL:
        return "queue full";
    case NDS_DEVICE_OPERATION_UNSUPPORTED:
        return "unsupported operation";
    default:
        return "unknown status";
    }
}

Result<void> allocate_operation_result(NpuRaContext *context, DeviceAllocation *allocation,
                                       nds_device_operation_request *operation) {
    if (!allocation->allocate(sizeof(nds_device_operation_result)))
        return unexpected(ErrorCode::kRuntime, context->error());
    nds_device_operation_result pending{NDS_DEVICE_OPERATION_INVALID_ARGUMENT,
                                        NDS_DEVICE_OPERATION_PATH_NONE, 0, 0U};
    if (!context->copy_host_to_device(allocation->get(), &pending, sizeof(pending)))
        return unexpected(ErrorCode::kRuntime, context->error());
    operation->operation_result_address = reinterpret_cast<std::uint64_t>(allocation->get());
    return {};
}

Result<void> check_operation_result(NpuRaContext *context, const DeviceAllocation &allocation) {
    nds_device_operation_result result{};
    if (!context->copy_device_to_host(&result, allocation.get(), sizeof(result)))
        return unexpected(ErrorCode::kRuntime, context->error());
    if (result.status != NDS_DEVICE_OPERATION_SUCCESS) {
        std::string message = "device dataplane operation failed: ";
        message += operation_status_name(result.status);
        if (result.path == NDS_DEVICE_OPERATION_PATH_PROVIDER)
            message += " (provider result " + std::to_string(result.provider_result) + ")";
        return unexpected(ErrorCode::kRuntime, std::move(message));
    }
    return {};
}

std::uint32_t host_ra_opcode(WorkRequestOpcode opcode) noexcept {
    switch (opcode) {
    case WorkRequestOpcode::Send:
        return NDS_RA_WR_SEND;
    case WorkRequestOpcode::RdmaRead:
        return NDS_RA_WR_RDMA_READ;
    case WorkRequestOpcode::RdmaWrite:
        return NDS_RA_WR_RDMA_WRITE;
    }
    return NDS_RA_WR_SEND;
}

std::uint32_t device_operation(WorkRequestOpcode opcode) noexcept {
    switch (opcode) {
    case WorkRequestOpcode::Send:
        return NDS_DEVICE_RDMA_SEND;
    case WorkRequestOpcode::RdmaRead:
        return NDS_DEVICE_RDMA_READ;
    case WorkRequestOpcode::RdmaWrite:
        return NDS_DEVICE_RDMA_WRITE;
    }
    return NDS_DEVICE_RDMA_SEND;
}

Result<void> validate_request(NpuExecutionMode execution, const WorkRequest &request) {
    const bool is_send = request.opcode == WorkRequestOpcode::Send;
    if (request.local.address == 0U || request.local.length == 0U || request.local.local_key == 0U) {
        return unexpected(ErrorCode::kInvalidArgument, "work request requires a registered local SGE");
    }
    if (!dataplane_supports(execution, request.opcode)) {
        return unexpected(ErrorCode::kUnsupported,
                          std::string(opcode_name(request.opcode)) + " is not supported by the execution mode");
    }
    if ((is_send && (request.remote_address != 0U || request.remote_key != 0U)) ||
        (!is_send && (request.remote_address == 0U || request.remote_key == 0U))) {
        return unexpected(ErrorCode::kInvalidArgument,
                          "work request remote-memory metadata does not match its opcode");
    }
    if (execution == NpuExecutionMode::Aicpu && request.local.length > kMaxAicpuTransferBytes) {
        return unexpected(ErrorCode::kInvalidArgument, "AICPU work request exceeds its maximum transfer length");
    }
    return {};
}

Result<void> post_aicpu_send_wr(NpuRaContext *context, NpuRaQp *qp, const RmaConfig &config,
                                const WorkRequest &request) {
    const auto connection = qp->make_device_connection();
    if (!connection) return unexpected(connection.error());
    nds_device_operation_request operation{};
    operation.operation = device_operation(request.opcode);
    operation.connection = *connection;
    operation.parameters.transfer = {1U,
                                     {request.local.address, request.local.length, request.local.local_key},
                                     request.remote_address, request.remote_key, 0U};
    DeviceAllocation result(context);
    if (const auto allocated = allocate_operation_result(context, &result, &operation); !allocated)
        return unexpected(allocated.error());
    AicpuConnectionLauncher launcher;
    if (!launcher.load(&context->acl_api(), config.aicpu_kernel_config) ||
        !launcher.launch_and_wait(&operation, 5000)) {
        return unexpected(ErrorCode::kRuntime, launcher.error());
    }
    return check_operation_result(context, result);
}

Result<void> post_aiv_send_wr(NpuRaContext *context, NpuRaQp *qp, const RmaConfig &config,
                              const WorkRequest &request) {
    const auto connection = qp->make_device_connection();
    if (!connection) return unexpected(connection.error());
    AivConnectionLauncher launcher;
    nds_device_operation_request device_record{};
    device_record.operation = device_operation(request.opcode);
    device_record.connection = *connection;
    device_record.parameters.transfer = {1U,
                                         {request.local.address, request.local.length, request.local.local_key},
                                         request.remote_address, request.remote_key, 0U};
    DeviceAllocation result(context);
    if (const auto allocated = allocate_operation_result(context, &result, &device_record); !allocated)
        return unexpected(allocated.error());
    if (!launcher.load(&context->acl_api(), config.aiv_kernel) ||
        !launcher.make_device_request(device_record, &device_record)) {
        return unexpected(ErrorCode::kRuntime, launcher.error());
    }
    DeviceAllocation device_request(context);
    if (!device_request.allocate(sizeof(device_record)) ||
        !context->copy_host_to_device(device_request.get(), &device_record, sizeof(device_record)) ||
        !launcher.launch_and_wait(reinterpret_cast<std::uint64_t>(device_request.get()), 5000)) {
        return unexpected(ErrorCode::kRuntime, launcher.error().empty() ? context->error() : launcher.error());
    }
    return check_operation_result(context, result);
}

}  // namespace

bool dataplane_supports(NpuExecutionMode execution, WorkRequestOpcode opcode) noexcept {
    switch (opcode) {
    case WorkRequestOpcode::Send:
    case WorkRequestOpcode::RdmaWrite:
        return execution == NpuExecutionMode::HostRa || execution == NpuExecutionMode::Aicpu ||
               execution == NpuExecutionMode::Aiv;
    case WorkRequestOpcode::RdmaRead:
        return execution == NpuExecutionMode::HostRa || execution == NpuExecutionMode::Aicpu ||
               execution == NpuExecutionMode::Aiv;
    }
    return false;
}

bool dataplane_supports_post_recv(NpuExecutionMode execution) noexcept {
    return execution == NpuExecutionMode::Aiv || execution == NpuExecutionMode::Aicpu;
}

bool dataplane_supports_cq_poll(NpuExecutionMode execution, CompletionQueue queue) noexcept {
    return (execution == NpuExecutionMode::HostRa && queue == CompletionQueue::Send) ||
           execution == NpuExecutionMode::Aiv || execution == NpuExecutionMode::Aicpu;
}

Result<void> post_send_wr(NpuRaContext *context, NpuRaQp *qp, NpuExecutionMode execution,
                          const RmaConfig &config, const WorkRequest &request) {
    if (context == nullptr || qp == nullptr)
        return unexpected(ErrorCode::kInvalidArgument, "RMA post requires a context and QP");
    if (qp->execution_mode() != execution)
        return unexpected(ErrorCode::kInvalidArgument, "work-request execution mode does not match the QP");
    if (const auto valid = validate_request(execution, request); !valid)
        return unexpected(valid.error());
    if (execution == NpuExecutionMode::HostRa) {
        return post_host_ra(context, qp,
                            {request.local, host_ra_opcode(request.opcode), request.remote_address,
                             request.remote_key});
    }
    if (execution == NpuExecutionMode::Aicpu)
        return post_aicpu_send_wr(context, qp, config, request);
    return post_aiv_send_wr(context, qp, config, request);
}

Result<void> post_recv_wr(NpuRaContext *context, NpuRaQp *qp, NpuExecutionMode execution,
                          const RmaConfig &config, const ReceiveRequest &request) {
    if (context == nullptr || qp == nullptr)
        return unexpected(ErrorCode::kInvalidArgument, "RMA receive post requires a context and QP");
    if (qp->execution_mode() != execution)
        return unexpected(ErrorCode::kInvalidArgument, "receive-request execution mode does not match the QP");
    if (request.local.address == 0U || request.local.length == 0U || request.local.local_key == 0U)
        return unexpected(ErrorCode::kInvalidArgument, "receive request requires a registered local SGE");
    if (!dataplane_supports_post_recv(execution))
        return unexpected(ErrorCode::kUnsupported,
                          "receive posting is not exposed by the selected NDS dataplane ABI");
    const auto connection = qp->make_device_connection();
    if (!connection) return unexpected(connection.error());
    nds_device_operation_request operation{};
    operation.operation = NDS_DEVICE_RDMA_RECV;
    operation.connection = *connection;
    operation.parameters.transfer = {request.wr_id,
                                     {request.local.address, request.local.length, request.local.local_key},
                                     0U, 0U, 0U};
    DeviceAllocation result(context);
    if (const auto allocated = allocate_operation_result(context, &result, &operation); !allocated)
        return unexpected(allocated.error());
    if (execution == NpuExecutionMode::Aicpu) {
        AicpuConnectionLauncher launcher;
        if (!launcher.load(&context->acl_api(), config.aicpu_kernel_config) ||
            !launcher.launch_and_wait(&operation, 5000))
            return unexpected(ErrorCode::kRuntime, launcher.error());
        return check_operation_result(context, result);
    }
    AivConnectionLauncher launcher;
    if (!launcher.load(&context->acl_api(), config.aiv_kernel) ||
        !launcher.make_device_request(operation, &operation))
        return unexpected(ErrorCode::kRuntime, launcher.error());
    DeviceAllocation device_request(context);
    if (!device_request.allocate(sizeof(operation)) ||
        !context->copy_host_to_device(device_request.get(), &operation, sizeof(operation)) ||
        !launcher.launch_and_wait(reinterpret_cast<std::uint64_t>(device_request.get()), 5000))
        return unexpected(ErrorCode::kRuntime, launcher.error().empty() ? context->error() : launcher.error());
    return check_operation_result(context, result);
}

Result<std::uint32_t> poll_cq(NpuRaContext *context, NpuRaQp *qp, NpuExecutionMode execution,
                              const RmaConfig &config, CompletionQueue queue,
                              nds_ra_completion *completions, std::uint32_t max_entries) {
    if (context == nullptr || qp == nullptr || completions == nullptr || max_entries == 0U)
        return unexpected(ErrorCode::kInvalidArgument, "RMA CQ poll requires a QP and completion storage");
    if (qp->execution_mode() != execution)
        return unexpected(ErrorCode::kInvalidArgument, "CQ-poll execution mode does not match the QP");
    if (!dataplane_supports_cq_poll(execution, queue))
        return unexpected(ErrorCode::kUnsupported, "CQ polling is not exposed by the selected NDS dataplane ABI");
    if (execution == NpuExecutionMode::HostRa)
        return poll_host_ra_cq(qp, completions, max_entries);
    const auto connection = qp->make_device_connection();
    if (!connection) return unexpected(connection.error());
    nds_device_operation_request operation{};
    operation.operation = NDS_DEVICE_POLL_CQ;
    operation.connection = *connection;
    operation.parameters.poll_cq.queue_kind =
        queue == CompletionQueue::Send ? NDS_DEVICE_SEND_QUEUE : NDS_DEVICE_RECEIVE_QUEUE;
    operation.parameters.poll_cq.max_completions =
        max_entries < NDS_DEVICE_MAX_COMPLETIONS ? max_entries : NDS_DEVICE_MAX_COMPLETIONS;
    DeviceAllocation result(context);
    if (const auto allocated = allocate_operation_result(context, &result, &operation); !allocated)
        return unexpected(allocated.error());
    DeviceAllocation output(context);
    if (!output.allocate(sizeof(nds_device_completion_output)))
        return unexpected(ErrorCode::kRuntime, context->error());
    operation.parameters.poll_cq.completion_output_address = reinterpret_cast<std::uint64_t>(output.get());
    if (execution == NpuExecutionMode::Aicpu) {
        AicpuConnectionLauncher launcher;
        if (!launcher.load(&context->acl_api(), config.aicpu_kernel_config) ||
            !launcher.launch_and_wait(&operation, 5000))
            return unexpected(ErrorCode::kRuntime, launcher.error());
    } else {
        AivConnectionLauncher launcher;
        DeviceAllocation device_request(context);
        if (!launcher.load(&context->acl_api(), config.aiv_kernel) ||
            !launcher.make_device_request(operation, &operation) ||
            !device_request.allocate(sizeof(operation)) ||
            !context->copy_host_to_device(device_request.get(), &operation, sizeof(operation)) ||
            !launcher.launch_and_wait(reinterpret_cast<std::uint64_t>(device_request.get()), 5000))
            return unexpected(ErrorCode::kRuntime, launcher.error().empty() ? context->error() : launcher.error());
    }
    if (const auto checked = check_operation_result(context, result); !checked)
        return unexpected(checked.error());
    nds_device_completion_output device_output{};
    if (!context->copy_device_to_host(&device_output, output.get(), sizeof(device_output)) ||
        device_output.count > operation.parameters.poll_cq.max_completions)
        return unexpected(ErrorCode::kRuntime, context->error().empty() ? "invalid CQ output" : context->error());
    for (std::uint32_t index = 0; index < device_output.count; ++index) {
        const auto &source = device_output.entries[index];
        completions[index] = {source.wr_id, source.status, source.opcode, source.vendor_error,
                              source.byte_length, source.qp_number, source.flags,
                              source.immediate_data_or_invalidated_rkey, {}, 0U};
    }
    return device_output.count;
}

}  // namespace nds

namespace nds::client {
namespace {

Result<void> post(Connection *session, WorkRequestOpcode opcode, const RegisteredRegion &local,
                  std::uint64_t remote_address, std::uint32_t remote_key, std::uint32_t length) {
    if (session == nullptr || session->qp() == nullptr || !local.belongs_to(session->qp()) || length == 0U ||
        length > local.length()) {
        return unexpected(ErrorCode::kInvalidArgument,
                          "connection post requires a local region from this session and a valid length");
    }
    return post_send_wr(session->context(), session->qp(), session->config().execution, session->config().rma,
                        {opcode, {local.address(), length, local.local_key()}, remote_address, remote_key});
}

Result<void> wait_for_send_completion(Connection *session, std::uint32_t timeout_ms) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        nds_ra_completion completion{};
        const auto count = poll(session, CompletionQueue::Send, &completion, 1U);
        if (!count)
            return unexpected(count.error());
        if (*count == 1U) {
            if (completion.status != 0)
                return unexpected(ErrorCode::kRa, "send completion reports an RNIC error");
            return {};
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return unexpected(ErrorCode::kRa, "timed out waiting for send CQ completion");
}

}  // namespace

Result<void> send(Connection *session, const RegisteredRegion &source, std::uint32_t length) {
    if (const auto posted = post(session, WorkRequestOpcode::Send, source, 0U, 0U, length); !posted)
        return unexpected(posted.error());
    return wait_for_send_completion(session, 5000U);
}

Result<void> post_receive(Connection *session, const RegisteredRegion &destination, std::uint64_t wr_id) {
    if (session == nullptr || session->qp() == nullptr || !destination.belongs_to(session->qp()))
        return unexpected(ErrorCode::kInvalidArgument,
                          "receive post requires a registered region from this session");
    return post_recv_wr(session->context(), session->qp(), session->config().execution, session->config().rma,
                        {{destination.address(), static_cast<std::uint32_t>(destination.length()),
                          destination.local_key()},
                         wr_id});
}

Result<std::uint32_t> poll(Connection *session, CompletionQueue queue, nds_ra_completion *completions,
                           std::uint32_t max_entries) {
    if (session == nullptr)
        return unexpected(ErrorCode::kInvalidArgument, "CQ poll requires a session");
    return poll_cq(session->context(), session->qp(), session->config().execution, session->config().rma, queue,
                   completions, max_entries);
}

Result<void> read(Connection *session, const RegisteredRegion &local, std::uint64_t remote_address,
                  std::uint32_t remote_key, std::uint32_t length) {
    return post(session, WorkRequestOpcode::RdmaRead, local, remote_address, remote_key, length);
}

Result<void> write(Connection *session, const RegisteredRegion &local, std::uint64_t remote_address,
                   std::uint32_t remote_key, std::uint32_t length) {
    return post(session, WorkRequestOpcode::RdmaWrite, local, remote_address, remote_key, length);
}

}  // namespace nds::client


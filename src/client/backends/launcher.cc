#include "launcher.hh"

#include "aicpu/launcher.hh"
#include "aicpu/launch_args.hh"
#include "aiv/launcher.hh"
#include "runtime.hh"

#include <cstddef>
#include <limits>
#include <string>

namespace nds::client {
namespace {

struct AivThreeAddressArguments {
    std::uint64_t first_address;
    std::uint64_t second_address;
    std::uint64_t return_value_address;
};

struct AivPollArguments {
    std::uint64_t qp_address;
    std::uint32_t is_send_cq;
    std::uint32_t max_completions;
    std::uint64_t wc_address;
    std::uint64_t return_value_address;
};

struct AivBatchArguments {
    std::uint64_t qp_address;
    std::uint64_t wrs_address;
    std::uint32_t wr_count;
    std::uint32_t reserved;
    std::uint64_t bad_wr_address;
    std::uint64_t return_value_address;
};

template <typename Request>
Result<void> launch_aicpu(Runtime *runtime, AicpuLauncher *launcher, const LaunchConfig &launch_config,
                          const char *entry, Request request, std::int32_t timeout_ms) {
    if (runtime == nullptr || launcher == nullptr)
        return Error{ErrorCode::kInvalidArgument, "AICPU launch requires a runtime and launcher"};
    std::int32_t return_value = std::numeric_limits<std::int32_t>::min();
    NDS_ASSIGN_OR_RETURN(MemoryBuffer device_return_value, runtime->allocate(sizeof(return_value)));
    NDS_RETURN_IF_ERROR(runtime->copy_to(&device_return_value, &return_value, sizeof(return_value)));
    NdsAicpuLaunchArgs<Request> arguments{request, reinterpret_cast<std::uint64_t>(device_return_value.data())};
    const int launch_result = launcher->launch(entry, launch_config, &arguments, sizeof(arguments));
    if (launch_result != ACL_SUCCESS)
        return Error{ErrorCode::kRuntime, "AICPU kernel launch failed: " + std::to_string(launch_result)};
    const int sync_result = aclrtSynchronizeStreamWithTimeout(launch_config.stream, timeout_ms);
    if (sync_result != ACL_SUCCESS)
        return Error{ErrorCode::kRuntime, "AICPU kernel synchronization failed: " + std::to_string(sync_result)};
    NDS_RETURN_IF_ERROR(runtime->copy_from(&return_value, device_return_value, sizeof(return_value)));
    return return_value == 0
               ? Result<void>{}
               : Error{ErrorCode::kRuntime, "AICPU device operation failed: " + std::to_string(return_value)};
}

template <typename Request>
Result<void> launch_aiv(Runtime *runtime, AivLauncher *launcher, const LaunchConfig &launch_config, const char *entry,
                        Request request, std::uint64_t first_offset, std::uint64_t second_offset,
                        std::int32_t timeout_ms) {
    if (runtime == nullptr || launcher == nullptr)
        return Error{ErrorCode::kInvalidArgument, "AIV launch requires a runtime and launcher"};
    NDS_ASSIGN_OR_RETURN(MemoryBuffer device_request, runtime->allocate(sizeof(request)));
    NDS_RETURN_IF_ERROR(runtime->copy_to(&device_request, &request, sizeof(request)));
    const std::uint64_t address = reinterpret_cast<std::uint64_t>(device_request.data());
    AivThreeAddressArguments arguments{address + first_offset, address + second_offset,
                                       address + offsetof(Request, return_value)};
    const int launch_result = launcher->launch(entry, launch_config, &arguments, sizeof(arguments));
    if (launch_result != ACL_SUCCESS)
        return Error{ErrorCode::kRuntime, "AIV kernel launch failed: " + std::to_string(launch_result)};
    const int sync_result = aclrtSynchronizeStreamWithTimeout(launch_config.stream, timeout_ms);
    if (sync_result != ACL_SUCCESS)
        return Error{ErrorCode::kRuntime, "AIV kernel synchronization failed: " + std::to_string(sync_result)};
    NDS_RETURN_IF_ERROR(runtime->copy_from(&request, device_request, sizeof(request)));
    return request.return_value == 0
               ? Result<void>{}
               : Error{ErrorCode::kRuntime, "AIV device operation failed: " + std::to_string(request.return_value)};
}

}  // namespace

BackendLauncher::~BackendLauncher() = default;

Result<NdsDeviceQp> BackendLauncher::describe_qp(const QueuePair &qp) const {
    if (!qp.created())
        return Error{ErrorCode::kInvalidArgument, "backend dispatch requires a created QP"};

    NdsDeviceQp descriptor{};
    descriptor.host_runtime_address = reinterpret_cast<std::uint64_t>(qp.endpoint_->runtime_);
    descriptor.host_qp_address = reinterpret_cast<std::uint64_t>(&qp);
    if (qp.backend_mode() != mode_)
        return Error{ErrorCode::kInvalidArgument, "QP backend mode does not match the backend dispatcher"};
    if (qp.backend_mode() == BackendMode::Ra)
        return descriptor;

    if (qp.ai_qp_info_.ai_qp_address == 0U)
        return Error{ErrorCode::kRa, "AI QP is missing provider metadata"};
    if (qp.backend_mode() == BackendMode::Aiv &&
        (qp.send_wr_ids_.data() == nullptr || qp.receive_wr_ids_.data() == nullptr)) {
        return Error{ErrorCode::kRuntime, "AIV QP is missing private WR-ID storage"};
    }
    const auto *source = reinterpret_cast<const NdsRaAiDataPlaneInfo *>(qp.ai_qp_info_.data_plane_info);
    if (source->send_wq.buffer_address == 0U || source->receive_wq.buffer_address == 0U)
        return Error{ErrorCode::kRa, "AI QP is missing SQ/RQ metadata"};

    const auto copy_wq = [](const NdsRaAiDataPlaneWq &input, std::uint64_t wr_id_address, bool is_send) {
        NdsDeviceWorkQueue output{};
        output.number = input.wqn;
        output.depth = input.depth;
        output.entry_size = input.wqebb_size;
        output.buffer_address = input.buffer_address;
        output.head_address = input.head_address;
        output.tail_address = input.tail_address;
        output.wr_id_address = wr_id_address;
        output.doorbell_mode = is_send ? NDS_DEVICE_DOORBELL_MMIO : NDS_DEVICE_DOORBELL_RECORD;
        output.doorbell_address = is_send ? input.doorbell_register_address : input.software_doorbell_address;
        return output;
    };
    const auto copy_cq = [](const NdsRaAiDataPlaneCq &input) {
        NdsDeviceCq output{};
        output.number = input.cqn;
        output.depth = input.depth;
        output.entry_size = input.cqe_size;
        output.buffer_address = input.buffer_address;
        output.consumer_address = input.tail_address;
        output.doorbell_mode = NDS_DEVICE_DOORBELL_RECORD;
        output.doorbell_address = input.software_doorbell_address;
        return output;
    };
    descriptor.flags = (qp.config_.control_flags & QueuePairCallerPollsCq) != 0U
                           ? static_cast<std::uint32_t>(NDS_DEVICE_QP_CALLER_POLLS_CQ)
                           : 0U;
    descriptor.qp_mode = qp.config_.ai_qp_mode;
    descriptor.service_level = qp.config_.service_level;
    descriptor.provider_qp_address = qp.ai_qp_info_.ai_qp_address;
    descriptor.provider_send_cq_address = qp.ai_qp_info_.ai_scq_address;
    descriptor.provider_receive_cq_address = qp.ai_qp_info_.ai_rcq_address;
    const std::uint64_t send_wr_ids =
        qp.backend_mode() == BackendMode::Aiv ? reinterpret_cast<std::uint64_t>(qp.send_wr_ids_.data()) : 0U;
    const std::uint64_t receive_wr_ids =
        qp.backend_mode() == BackendMode::Aiv ? reinterpret_cast<std::uint64_t>(qp.receive_wr_ids_.data()) : 0U;
    descriptor.send_queue = copy_wq(source->send_wq, send_wr_ids, true);
    descriptor.receive_queue = copy_wq(source->receive_wq, receive_wr_ids, false);
    descriptor.send_cq = copy_cq(source->send_cq);
    descriptor.receive_cq = copy_cq(source->receive_cq);
    return descriptor;
}

Result<void> BackendLauncher::open(Runtime *runtime, BackendMode mode, const std::string &artifact) {
    if (runtime == nullptr || !runtime->initialized() || ra_ != nullptr || aiv_ != nullptr || aicpu_ != nullptr ||
        mode_ != BackendMode::Ra)
        return Error{ErrorCode::kInvalidArgument, "backend dispatcher is already open"};
    runtime_ = runtime;
    mode_ = mode;
    if (mode == BackendMode::Ra) {
        ra_ = std::make_unique<RaLauncher>();
        const Result<void> load_result = ra_->load(artifact);
        if (!load_result.ok()) {
            ra_.reset();
            return load_result.error();
        }
        return {};
    }
    if (mode == BackendMode::Aiv) {
        aiv_ = std::make_unique<AivLauncher>();
        const Result<void> load_result = aiv_->load(artifact);
        if (!load_result.ok()) {
            aiv_.reset();
            mode_ = BackendMode::Ra;
            return load_result.error();
        }
        return {};
    }
    aicpu_ = std::make_unique<AicpuLauncher>();
    const Result<void> load_result = aicpu_->load(artifact);
    if (!load_result.ok()) {
        aicpu_.reset();
        mode_ = BackendMode::Ra;
        return load_result.error();
    }
    return {};
}

Result<void> BackendLauncher::post_send(const LaunchConfig &launch_config, const NdsDeviceQp &qp,
                                        const NdsDeviceSendWr &wr, std::int32_t timeout_ms) {
    const NdsDevicePostSendArgs request{qp, wr, std::numeric_limits<std::int32_t>::min()};
    if (aicpu_ != nullptr)
        return launch_aicpu(runtime_, aicpu_.get(), launch_config, "nds_aicpu_post_send_kernel", request, timeout_ms);
    if (aiv_ != nullptr)
        return launch_aiv(runtime_, aiv_.get(), launch_config, "nds_aiv_post_send_kernel", request,
                          offsetof(NdsDevicePostSendArgs, qp), offsetof(NdsDevicePostSendArgs, wr), timeout_ms);
    if (ra_ != nullptr)
        return ra_->post_send(qp, wr, launch_config.stream);
    return Error{ErrorCode::kRuntime, "backend dispatcher is not open"};
}

Result<void> BackendLauncher::post_recv(const LaunchConfig &launch_config, const NdsDeviceQp &qp,
                                        const NdsDeviceRecvWr &wr, std::int32_t timeout_ms) {
    const NdsDevicePostRecvArgs request{qp, wr, std::numeric_limits<std::int32_t>::min()};
    if (aicpu_ != nullptr)
        return launch_aicpu(runtime_, aicpu_.get(), launch_config, "nds_aicpu_post_recv_kernel", request, timeout_ms);
    if (aiv_ != nullptr)
        return launch_aiv(runtime_, aiv_.get(), launch_config, "nds_aiv_post_recv_kernel", request,
                          offsetof(NdsDevicePostRecvArgs, qp), offsetof(NdsDevicePostRecvArgs, wr), timeout_ms);
    if (ra_ != nullptr)
        return ra_->post_recv(qp, wr);
    return Error{ErrorCode::kRuntime, "backend dispatcher is not open"};
}

Result<void> BackendLauncher::post_send_batch(Runtime *runtime, const NdsDeviceQp &qp,
                                              std::span<const NdsDeviceSendWr> wrs, std::int32_t timeout_ms) {
    if (runtime == nullptr)
        return Error{ErrorCode::kInvalidArgument, "AIV send batching requires a runtime"};
    if (wrs.empty())
        return Error{ErrorCode::kInvalidArgument, "AI send batch requires work requests"};
    if (aicpu_ != nullptr)
        return Error{ErrorCode::kUnsupported, "AICPU send batching is not implemented"};
    if (aiv_ == nullptr)
        return Error{ErrorCode::kUnsupported, "RA send batching is not implemented"};
    NDS_ASSIGN_OR_RETURN(MemoryBuffer device_wrs, runtime->allocate(wrs.size_bytes()));
    NDS_RETURN_IF_ERROR(runtime->copy_to(&device_wrs, wrs.data(), wrs.size_bytes()));
    NdsDevicePostSendBatchArgs request{qp, reinterpret_cast<std::uint64_t>(device_wrs.data()),
                                       static_cast<std::uint32_t>(wrs.size()), std::numeric_limits<std::int32_t>::min(),
                                       0U};
    NDS_ASSIGN_OR_RETURN(MemoryBuffer device_request, runtime->allocate(sizeof(request)));
    NDS_RETURN_IF_ERROR(runtime->copy_to(&device_request, &request, sizeof(request)));
    const std::uint64_t address = reinterpret_cast<std::uint64_t>(device_request.data());
    AivBatchArguments arguments{address + offsetof(NdsDevicePostSendBatchArgs, qp),
                                request.wrs_address,
                                request.wr_count,
                                0U,
                                address + offsetof(NdsDevicePostSendBatchArgs, bad_wr_address),
                                address + offsetof(NdsDevicePostSendBatchArgs, return_value)};
    (void)timeout_ms;
    return Error{ErrorCode::kUnsupported, "AIV send batching requires an explicit LaunchConfig"};
}

Result<std::uint32_t> BackendLauncher::poll_cq(const LaunchConfig &launch_config, const NdsDeviceQp &qp, bool send_cq,
                                               std::uint32_t max_completions, NdsDeviceWc *completions,
                                               std::int32_t timeout_ms) {
    if (completions == nullptr || max_completions == 0U)
        return Error{ErrorCode::kInvalidArgument, "invalid device CQ poll arguments"};
    if (ra_ != nullptr)
        return ra_->poll_cq(qp, send_cq ? 1U : 0U, max_completions, completions);
    if (runtime_ == nullptr)
        return Error{ErrorCode::kRuntime, "backend launcher has no runtime"};
    NDS_ASSIGN_OR_RETURN(MemoryBuffer device_completions, runtime_->allocate(max_completions * sizeof(*completions)));
    NdsDevicePollCqArgs request{qp, send_cq ? 1U : 0U, max_completions,
                                reinterpret_cast<std::uint64_t>(device_completions.data()),
                                std::numeric_limits<std::int32_t>::min()};
    if (aicpu_ != nullptr) {
        NDS_ASSIGN_OR_RETURN(MemoryBuffer device_return_value, runtime_->allocate(sizeof(request.return_value)));
        NDS_RETURN_IF_ERROR(
            runtime_->copy_to(&device_return_value, &request.return_value, sizeof(request.return_value)));
        NdsAicpuLaunchArgs<NdsDevicePollCqArgs> arguments{request,
                                                          reinterpret_cast<std::uint64_t>(device_return_value.data())};
        const int launch_result =
            aicpu_->launch("nds_aicpu_poll_cq_kernel", launch_config, &arguments, sizeof(arguments));
        if (launch_result != ACL_SUCCESS)
            return Error{ErrorCode::kRuntime, "AICPU CQ poll launch failed: " + std::to_string(launch_result)};
        const int sync_result = aclrtSynchronizeStreamWithTimeout(launch_config.stream, timeout_ms);
        if (sync_result != ACL_SUCCESS)
            return Error{ErrorCode::kRuntime, "AICPU CQ poll synchronization failed: " + std::to_string(sync_result)};
        NDS_RETURN_IF_ERROR(
            runtime_->copy_from(&request.return_value, device_return_value, sizeof(request.return_value)));
        if (request.return_value < 0)
            return Error{ErrorCode::kRuntime, "device CQ poll failed: " + std::to_string(request.return_value)};
        const std::uint32_t count = static_cast<std::uint32_t>(request.return_value);
        if (count != 0U)
            NDS_RETURN_IF_ERROR(runtime_->copy_from(completions, device_completions, count * sizeof(*completions)));
        return count;
    }

    if (aiv_ == nullptr)
        return Error{ErrorCode::kRuntime, "backend dispatcher is not open"};

    NDS_ASSIGN_OR_RETURN(MemoryBuffer device_request, runtime_->allocate(sizeof(request)));
    NDS_RETURN_IF_ERROR(runtime_->copy_to(&device_request, &request, sizeof(request)));
    const std::uint64_t address = reinterpret_cast<std::uint64_t>(device_request.data());
    AivPollArguments arguments{address + offsetof(NdsDevicePollCqArgs, qp), send_cq ? 1U : 0U, max_completions,
                               reinterpret_cast<std::uint64_t>(device_completions.data()),
                               address + offsetof(NdsDevicePollCqArgs, return_value)};
    const int launch_result = aiv_->launch("nds_aiv_poll_cq_kernel", launch_config, &arguments, sizeof(arguments));
    if (launch_result != ACL_SUCCESS)
        return Error{ErrorCode::kRuntime, "AIV CQ poll launch failed: " + std::to_string(launch_result)};
    const int sync_result = aclrtSynchronizeStreamWithTimeout(launch_config.stream, timeout_ms);
    if (sync_result != ACL_SUCCESS)
        return Error{ErrorCode::kRuntime, "AIV CQ poll synchronization failed: " + std::to_string(sync_result)};
    NDS_RETURN_IF_ERROR(runtime_->copy_from(&request, device_request, sizeof(request)));
    if (request.return_value < 0)
        return Error{ErrorCode::kRuntime, "device CQ poll failed: " + std::to_string(request.return_value)};
    const std::uint32_t count = static_cast<std::uint32_t>(request.return_value);
    if (count != 0U) {
        NDS_RETURN_IF_ERROR(runtime_->copy_from(completions, device_completions, count * sizeof(*completions)));
    }
    return count;
}

Result<void> BackendLauncher::storage_read(Runtime *runtime, const NdsDeviceStorageContext &context,
                                           const StorageReadCommand &command, std::int32_t timeout_ms) {
    (void)runtime;
    (void)context;
    (void)command;
    (void)timeout_ms;
    return Error{ErrorCode::kUnsupported, "storage requires its own explicit stream-aware launcher API"};
}

Result<void> BackendLauncher::storage_write(Runtime *runtime, const NdsDeviceStorageContext &context,
                                            const StorageWriteCommand &command, std::int32_t timeout_ms) {
    (void)runtime;
    (void)context;
    (void)command;
    (void)timeout_ms;
    return Error{ErrorCode::kUnsupported, "storage requires its own explicit stream-aware launcher API"};
}

Result<void> BackendLauncher::storage_batch_read(Runtime *runtime, const NdsDeviceStorageContext &context,
                                                 const StorageBatchReadCommand &command, std::int32_t timeout_ms) {
    (void)runtime;
    (void)context;
    (void)command;
    (void)timeout_ms;
    return Error{ErrorCode::kUnsupported, "storage requires its own explicit stream-aware launcher API"};
}

Result<void> BackendLauncher::storage_batch_write(Runtime *runtime, const NdsDeviceStorageContext &context,
                                                  const StorageBatchWriteCommand &command, std::int32_t timeout_ms) {
    (void)runtime;
    (void)context;
    (void)command;
    (void)timeout_ms;
    return Error{ErrorCode::kUnsupported, "storage requires its own explicit stream-aware launcher API"};
}

}  // namespace nds::client

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

template <typename Request>
Result<void> launch_aicpu(Runtime *runtime, AicpuLauncher *launcher, const LaunchConfig &launch_config,
                          const char *entry, Request request, std::int32_t timeout_ms) {
    if (runtime == nullptr || launcher == nullptr)
        return Error{ErrorCode::kInvalidArgument, "AICPU launch requires a runtime and launcher"};
    std::int32_t return_value = std::numeric_limits<std::int32_t>::min();
    NDS_ASSIGN_OR_RETURN(MemoryBuffer device_return_value,
                         runtime->allocate(sizeof(return_value), MemoryLocation::Device));
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
    NDS_ASSIGN_OR_RETURN(MemoryBuffer device_request, runtime->allocate(sizeof(request), MemoryLocation::Device));
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

Result<std::uint32_t> BackendLauncher::poll_cq(const LaunchConfig &launch_config, const NdsDeviceQp &qp, bool send_cq,
                                               std::uint32_t max_completions, NdsDeviceWc *completions,
                                               std::int32_t timeout_ms) {
    if (completions == nullptr || max_completions == 0U)
        return Error{ErrorCode::kInvalidArgument, "invalid device CQ poll arguments"};
    if (ra_ != nullptr)
        return ra_->poll_cq(qp, send_cq ? 1U : 0U, max_completions, completions);
    if (runtime_ == nullptr)
        return Error{ErrorCode::kRuntime, "backend launcher has no runtime"};
    NDS_ASSIGN_OR_RETURN(MemoryBuffer device_completions,
                         runtime_->allocate(max_completions * sizeof(*completions), MemoryLocation::Device));
    NdsDevicePollCqArgs request{qp, send_cq ? 1U : 0U, max_completions,
                                reinterpret_cast<std::uint64_t>(device_completions.data()),
                                std::numeric_limits<std::int32_t>::min()};
    if (aicpu_ != nullptr) {
        NDS_ASSIGN_OR_RETURN(MemoryBuffer device_return_value,
                             runtime_->allocate(sizeof(request.return_value), MemoryLocation::Device));
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

    NDS_ASSIGN_OR_RETURN(MemoryBuffer device_request, runtime_->allocate(sizeof(request), MemoryLocation::Device));
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

}  // namespace nds::client

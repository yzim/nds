#include "dispatcher.hh"

#include "aicpu/launcher.hh"
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
Result<void> launch_aicpu(Runtime &runtime, AicpuLauncher &launcher, const char *entry, Request request,
                          std::int32_t timeout_ms) {
    auto device_request = runtime.allocate(sizeof(request));
    if (!device_request)
        return unexpected(device_request.error());
    if (const auto copied = runtime.copy_to(&*device_request, &request, sizeof(request)); !copied)
        return unexpected(copied.error());
    if (const auto launched = launcher.launch(entry, reinterpret_cast<std::uint64_t>(device_request->data()));
        !launched)
        return unexpected(launched.error());
    if (const auto synchronized = launcher.synchronize(timeout_ms); !synchronized)
        return unexpected(synchronized.error());
    if (const auto copied = runtime.copy_from(&request, *device_request, sizeof(request)); !copied)
        return unexpected(copied.error());
    return request.return_value == 0 ? Result<void>{}
                                     : unexpected(ErrorCode::kRuntime, "AICPU device operation failed: " +
                                                                           std::to_string(request.return_value));
}

template <typename Request>
Result<void> launch_aiv(Runtime &runtime, AivLauncher &launcher, const char *entry, Request request,
                        std::uint64_t first_offset, std::uint64_t second_offset, std::int32_t timeout_ms) {
    auto device_request = runtime.allocate(sizeof(request));
    if (!device_request)
        return unexpected(device_request.error());
    if (const auto copied = runtime.copy_to(&*device_request, &request, sizeof(request)); !copied)
        return unexpected(copied.error());
    const std::uint64_t address = reinterpret_cast<std::uint64_t>(device_request->data());
    AivThreeAddressArguments arguments{address + first_offset, address + second_offset,
                                       address + offsetof(Request, return_value)};
    if (const auto launched = launcher.launch(entry, &arguments, sizeof(arguments)); !launched)
        return unexpected(launched.error());
    if (const auto synchronized = launcher.synchronize(timeout_ms); !synchronized)
        return unexpected(synchronized.error());
    if (const auto copied = runtime.copy_from(&request, *device_request, sizeof(request)); !copied)
        return unexpected(copied.error());
    return request.return_value == 0 ? Result<void>{}
                                     : unexpected(ErrorCode::kRuntime, "AIV device operation failed: " +
                                                                           std::to_string(request.return_value));
}

}  // namespace

BackendDispatcher::~BackendDispatcher() = default;

Result<void> BackendDispatcher::open(NpuBackend mode, const std::string &aiv_kernel, const std::string &aicpu_kernel) {
    if (aiv_ != nullptr || aicpu_ != nullptr || mode_ != NpuBackend::Ra)
        return unexpected(ErrorCode::kInvalidArgument, "backend dispatcher is already open");
    mode_ = mode;
    if (mode == NpuBackend::Ra)
        return {};
    if (mode == NpuBackend::Aiv) {
        aiv_ = std::make_unique<AivLauncher>();
        if (const auto result = aiv_->load(aiv_kernel); !result) {
            aiv_.reset();
            mode_ = NpuBackend::Ra;
            return unexpected(result.error());
        }
        return {};
    }
    aicpu_ = std::make_unique<AicpuLauncher>();
    if (const auto result = aicpu_->load(aicpu_kernel); !result) {
        aicpu_.reset();
        mode_ = NpuBackend::Ra;
        return unexpected(result.error());
    }
    return {};
}

Result<void> BackendDispatcher::post_send(Runtime &runtime, const NdsDeviceQp &qp, const NdsDeviceSendWr &wr,
                                          std::int32_t timeout_ms) {
    const NdsDevicePostSendArgs request{qp, wr, std::numeric_limits<std::int32_t>::min()};
    if (aicpu_ != nullptr)
        return launch_aicpu(runtime, *aicpu_, "nds_aicpu_post_send_kernel", request, timeout_ms);
    if (aiv_ != nullptr)
        return launch_aiv(runtime, *aiv_, "nds_aiv_post_send_kernel", request, offsetof(NdsDevicePostSendArgs, qp),
                          offsetof(NdsDevicePostSendArgs, wr), timeout_ms);
    return unexpected(ErrorCode::kInvalidArgument, "RA does not use the AI backend dispatcher");
}

Result<void> BackendDispatcher::post_recv(Runtime &runtime, const NdsDeviceQp &qp, const NdsDeviceRecvWr &wr,
                                          std::int32_t timeout_ms) {
    const NdsDevicePostRecvArgs request{qp, wr, std::numeric_limits<std::int32_t>::min()};
    if (aicpu_ != nullptr)
        return launch_aicpu(runtime, *aicpu_, "nds_aicpu_post_recv_kernel", request, timeout_ms);
    if (aiv_ != nullptr)
        return launch_aiv(runtime, *aiv_, "nds_aiv_post_recv_kernel", request, offsetof(NdsDevicePostRecvArgs, qp),
                          offsetof(NdsDevicePostRecvArgs, wr), timeout_ms);
    return unexpected(ErrorCode::kInvalidArgument, "RA does not use the AI backend dispatcher");
}

Result<void> BackendDispatcher::post_send_batch(Runtime &runtime, const NdsDeviceQp &qp,
                                                std::span<const NdsDeviceSendWr> wrs, std::int32_t timeout_ms) {
    if (wrs.empty())
        return unexpected(ErrorCode::kInvalidArgument, "AI send batch requires work requests");
    if (aicpu_ != nullptr)
        return unexpected(ErrorCode::kUnsupported, "AICPU send batching is not implemented");
    if (aiv_ == nullptr)
        return unexpected(ErrorCode::kInvalidArgument, "RA does not use the AI backend dispatcher");
    auto device_wrs = runtime.allocate(wrs.size_bytes());
    if (!device_wrs)
        return unexpected(device_wrs.error());
    if (const auto copied = runtime.copy_to(&*device_wrs, wrs.data(), wrs.size_bytes()); !copied)
        return unexpected(copied.error());
    NdsDevicePostSendBatchArgs request{qp, reinterpret_cast<std::uint64_t>(device_wrs->data()),
                                       static_cast<std::uint32_t>(wrs.size()), std::numeric_limits<std::int32_t>::min(),
                                       0U};
    auto device_request = runtime.allocate(sizeof(request));
    if (!device_request)
        return unexpected(device_request.error());
    if (const auto copied = runtime.copy_to(&*device_request, &request, sizeof(request)); !copied)
        return unexpected(copied.error());
    const std::uint64_t address = reinterpret_cast<std::uint64_t>(device_request->data());
    AivBatchArguments arguments{address + offsetof(NdsDevicePostSendBatchArgs, qp),
                                request.wrs_address,
                                request.wr_count,
                                0U,
                                address + offsetof(NdsDevicePostSendBatchArgs, bad_wr_address),
                                address + offsetof(NdsDevicePostSendBatchArgs, return_value)};
    if (const auto launched = aiv_->launch("nds_aiv_post_send_batch_kernel", &arguments, sizeof(arguments)); !launched)
        return unexpected(launched.error());
    if (const auto synchronized = aiv_->synchronize(timeout_ms); !synchronized)
        return unexpected(synchronized.error());
    if (const auto copied = runtime.copy_from(&request, *device_request, sizeof(request)); !copied)
        return unexpected(copied.error());
    return request.return_value == 0
               ? Result<void>{}
               : unexpected(ErrorCode::kRuntime, "AIV send batch failed: " + std::to_string(request.return_value));
}

Result<void> BackendDispatcher::rdma_send(Runtime &runtime, const NdsDeviceTransport &transport,
                                          const NdsDeviceSendWr &wr, std::int32_t timeout_ms) {
    const NdsDeviceRdmaSendArgs request{transport, wr, std::numeric_limits<std::int32_t>::min()};
    if (aicpu_ != nullptr)
        return launch_aicpu(runtime, *aicpu_, "nds_aicpu_rdma_send_kernel", request, timeout_ms);
    if (aiv_ != nullptr)
        return launch_aiv(runtime, *aiv_, "nds_aiv_rdma_send_kernel", request,
                          offsetof(NdsDeviceRdmaSendArgs, transport), offsetof(NdsDeviceRdmaSendArgs, wr), timeout_ms);
    return unexpected(ErrorCode::kInvalidArgument, "RA does not use the AI backend dispatcher");
}

Result<void> BackendDispatcher::rdma_recv(Runtime &runtime, const NdsDeviceTransport &transport,
                                          const NdsDeviceRecvWr &wr, std::int32_t timeout_ms) {
    const NdsDeviceRdmaRecvArgs request{transport, wr, std::numeric_limits<std::int32_t>::min()};
    if (aicpu_ != nullptr)
        return launch_aicpu(runtime, *aicpu_, "nds_aicpu_rdma_recv_kernel", request, timeout_ms);
    if (aiv_ != nullptr)
        return launch_aiv(runtime, *aiv_, "nds_aiv_rdma_recv_kernel", request,
                          offsetof(NdsDeviceRdmaRecvArgs, transport), offsetof(NdsDeviceRdmaRecvArgs, wr), timeout_ms);
    return unexpected(ErrorCode::kInvalidArgument, "RA does not use the AI backend dispatcher");
}

Result<void> BackendDispatcher::rdma_read(Runtime &runtime, const NdsDeviceTransport &transport,
                                          const NdsDeviceSendWr &wr, std::int32_t timeout_ms) {
    const NdsDeviceRdmaReadArgs request{transport, wr, std::numeric_limits<std::int32_t>::min()};
    if (aicpu_ != nullptr)
        return launch_aicpu(runtime, *aicpu_, "nds_aicpu_rdma_read_kernel", request, timeout_ms);
    if (aiv_ != nullptr)
        return launch_aiv(runtime, *aiv_, "nds_aiv_rdma_read_kernel", request,
                          offsetof(NdsDeviceRdmaReadArgs, transport), offsetof(NdsDeviceRdmaReadArgs, wr), timeout_ms);
    return unexpected(ErrorCode::kInvalidArgument, "RA does not use the AI backend dispatcher");
}

Result<void> BackendDispatcher::rdma_write(Runtime &runtime, const NdsDeviceTransport &transport,
                                           const NdsDeviceSendWr &wr, std::int32_t timeout_ms) {
    const NdsDeviceRdmaWriteArgs request{transport, wr, std::numeric_limits<std::int32_t>::min()};
    if (aicpu_ != nullptr)
        return launch_aicpu(runtime, *aicpu_, "nds_aicpu_rdma_write_kernel", request, timeout_ms);
    if (aiv_ != nullptr)
        return launch_aiv(runtime, *aiv_, "nds_aiv_rdma_write_kernel", request,
                          offsetof(NdsDeviceRdmaWriteArgs, transport), offsetof(NdsDeviceRdmaWriteArgs, wr),
                          timeout_ms);
    return unexpected(ErrorCode::kInvalidArgument, "RA does not use the AI backend dispatcher");
}

Result<std::uint32_t> BackendDispatcher::poll_cq(Runtime &runtime, const NdsDeviceQp &qp, bool send_cq,
                                                 std::uint32_t max_completions, NdsDeviceWc *completions,
                                                 std::int32_t timeout_ms) {
    if (completions == nullptr || max_completions == 0U)
        return unexpected(ErrorCode::kInvalidArgument, "invalid device CQ poll arguments");
    auto device_completions = runtime.allocate(max_completions * sizeof(*completions));
    if (!device_completions)
        return unexpected(device_completions.error());
    NdsDevicePollCqArgs request{qp, send_cq ? 1U : 0U, max_completions,
                                reinterpret_cast<std::uint64_t>(device_completions->data()),
                                std::numeric_limits<std::int32_t>::min()};
    auto device_request = runtime.allocate(sizeof(request));
    if (!device_request)
        return unexpected(device_request.error());
    if (const auto copied = runtime.copy_to(&*device_request, &request, sizeof(request)); !copied)
        return unexpected(copied.error());
    Result<void> launched = unexpected(ErrorCode::kInvalidArgument, "RA does not use the AI backend dispatcher");
    if (aicpu_ != nullptr) {
        launched = aicpu_->launch("nds_aicpu_poll_cq_kernel", reinterpret_cast<std::uint64_t>(device_request->data()));
    } else if (aiv_ != nullptr) {
        const std::uint64_t address = reinterpret_cast<std::uint64_t>(device_request->data());
        AivPollArguments arguments{address + offsetof(NdsDevicePollCqArgs, qp), send_cq ? 1U : 0U, max_completions,
                                   reinterpret_cast<std::uint64_t>(device_completions->data()),
                                   address + offsetof(NdsDevicePollCqArgs, return_value)};
        launched = aiv_->launch("nds_aiv_poll_cq_kernel", &arguments, sizeof(arguments));
    }
    if (!launched)
        return unexpected(launched.error());
    const auto synchronized = aicpu_ != nullptr ? aicpu_->synchronize(timeout_ms) : aiv_->synchronize(timeout_ms);
    if (!synchronized)
        return unexpected(synchronized.error());
    if (const auto copied = runtime.copy_from(&request, *device_request, sizeof(request)); !copied)
        return unexpected(copied.error());
    if (request.return_value < 0)
        return unexpected(ErrorCode::kRuntime, "device CQ poll failed: " + std::to_string(request.return_value));
    const auto count = static_cast<std::uint32_t>(request.return_value);
    if (count != 0U) {
        if (const auto copied = runtime.copy_from(completions, *device_completions, count * sizeof(*completions));
            !copied)
            return unexpected(copied.error());
    }
    return count;
}

Result<void> BackendDispatcher::storage_read(Runtime &runtime, const NdsDeviceStorageContext &context,
                                             const StorageReadCommand &command, std::int32_t timeout_ms) {
    const NdsDeviceStorageReadArgs request{context, command, std::numeric_limits<std::int32_t>::min()};
    if (aicpu_ != nullptr)
        return launch_aicpu(runtime, *aicpu_, "nds_aicpu_storage_read_kernel", request, timeout_ms);
    if (aiv_ != nullptr)
        return launch_aiv(runtime, *aiv_, "nds_aiv_storage_read_kernel", request,
                          offsetof(NdsDeviceStorageReadArgs, context), offsetof(NdsDeviceStorageReadArgs, command),
                          timeout_ms);
    return unexpected(ErrorCode::kInvalidArgument, "RA does not use the AI backend dispatcher");
}

Result<void> BackendDispatcher::storage_write(Runtime &runtime, const NdsDeviceStorageContext &context,
                                              const StorageWriteCommand &command, std::int32_t timeout_ms) {
    const NdsDeviceStorageWriteArgs request{context, command, std::numeric_limits<std::int32_t>::min()};
    if (aicpu_ != nullptr)
        return launch_aicpu(runtime, *aicpu_, "nds_aicpu_storage_write_kernel", request, timeout_ms);
    if (aiv_ != nullptr)
        return launch_aiv(runtime, *aiv_, "nds_aiv_storage_write_kernel", request,
                          offsetof(NdsDeviceStorageWriteArgs, context), offsetof(NdsDeviceStorageWriteArgs, command),
                          timeout_ms);
    return unexpected(ErrorCode::kInvalidArgument, "RA does not use the AI backend dispatcher");
}

Result<void> BackendDispatcher::storage_batch_read(Runtime &runtime, const NdsDeviceStorageContext &context,
                                                   const StorageBatchReadCommand &command, std::int32_t timeout_ms) {
    const NdsDeviceStorageBatchReadArgs request{context, command, std::numeric_limits<std::int32_t>::min()};
    if (aicpu_ != nullptr)
        return launch_aicpu(runtime, *aicpu_, "nds_aicpu_storage_batch_read_kernel", request, timeout_ms);
    if (aiv_ != nullptr)
        return launch_aiv(runtime, *aiv_, "nds_aiv_storage_batch_read_kernel", request,
                          offsetof(NdsDeviceStorageBatchReadArgs, context),
                          offsetof(NdsDeviceStorageBatchReadArgs, command), timeout_ms);
    return unexpected(ErrorCode::kInvalidArgument, "RA does not use the AI backend dispatcher");
}

Result<void> BackendDispatcher::storage_batch_write(Runtime &runtime, const NdsDeviceStorageContext &context,
                                                    const StorageBatchWriteCommand &command, std::int32_t timeout_ms) {
    const NdsDeviceStorageBatchWriteArgs request{context, command, std::numeric_limits<std::int32_t>::min()};
    if (aicpu_ != nullptr)
        return launch_aicpu(runtime, *aicpu_, "nds_aicpu_storage_batch_write_kernel", request, timeout_ms);
    if (aiv_ != nullptr)
        return launch_aiv(runtime, *aiv_, "nds_aiv_storage_batch_write_kernel", request,
                          offsetof(NdsDeviceStorageBatchWriteArgs, context),
                          offsetof(NdsDeviceStorageBatchWriteArgs, command), timeout_ms);
    return unexpected(ErrorCode::kInvalidArgument, "RA does not use the AI backend dispatcher");
}

}  // namespace nds::client

#include "launcher.hh"

#include "runtime.hh"

#include <cstddef>
#include <limits>
#include <string>

namespace nds::client {

namespace {

template <typename WorkRequest>
struct PostArguments {
    NdsQpDescriptor qp;
    WorkRequest work_request;
    std::uint64_t return_value_address;
};

struct PollArguments {
    std::uint64_t qp_address;
    std::uint32_t is_send_cq;
    std::uint32_t max_completions;
    std::uint64_t wc_address;
    std::uint64_t return_value_address;
};

struct BatchArguments {
    std::uint64_t qp_address;
    std::uint64_t wrs_address;
    std::uint32_t wr_count;
    std::uint64_t bad_wr_address;
    std::uint64_t return_value_address;
};

struct TransportBatchArguments {
    std::uint64_t transport_address;
    std::uint64_t wrs_address;
    std::uint32_t queue_index;
    std::uint32_t wr_count;
    std::uint64_t bad_wr_address;
    std::uint64_t return_value_address;
};

struct TransportArguments {
    std::uint64_t transport_address;
    std::uint64_t work_request_address;
    std::uint32_t queue_index;
    std::uint32_t reserved;
    std::uint64_t return_value_address;
};

struct StorageArguments {
    std::uint64_t storage_address;
    std::uint64_t command_address;
    std::uint64_t return_value_address;
};

template <typename WorkRequest>
Result<void> launch_post_and_wait(Runtime *runtime, const AivLauncher *launcher, const LaunchConfig &config,
                                  const char *entry, const NdsQpDescriptor &qp, const WorkRequest &work_request) {
    if (runtime == nullptr || launcher == nullptr)
        return Error{ErrorCode::kRuntime, "AIV backend is not loaded"};
    std::int32_t return_value = std::numeric_limits<std::int32_t>::min();
    NDS_ASSIGN_OR_RETURN(MemoryBuffer device_return_value,
                         runtime->allocate(sizeof(return_value), MemoryLocation::Device));
    NDS_RETURN_IF_ERROR(runtime->copy_to(&device_return_value, &return_value, sizeof(return_value)));
    PostArguments<WorkRequest> arguments{qp, work_request, reinterpret_cast<std::uint64_t>(device_return_value.data())};
    const int launch_result = launcher->launch(entry, config, &arguments, sizeof(arguments));
    if (launch_result != ACL_SUCCESS)
        return Error{ErrorCode::kRuntime, "AIV kernel launch failed: " + std::to_string(launch_result)};
    const int sync_result = aclrtSynchronizeStreamWithTimeout(config.stream, config.sync_timeout_ms);
    if (sync_result != ACL_SUCCESS)
        return Error{ErrorCode::kRuntime, "AIV kernel synchronization failed: " + std::to_string(sync_result)};
    NDS_RETURN_IF_ERROR(runtime->copy_from(&return_value, device_return_value, sizeof(return_value)));
    return return_value == 0
               ? Result<void>{}
               : Error{ErrorCode::kRuntime, "AIV device operation failed: " + std::to_string(return_value)};
}

Result<PostSendBatchResult> launch_post_batch_and_wait(Runtime *runtime, const AivLauncher *launcher,
                                                       const LaunchConfig &config, const NdsQpDescriptor &qp,
                                                       std::span<const NdsSendWr> wrs) {
    if (runtime == nullptr || launcher == nullptr)
        return Error{ErrorCode::kRuntime, "AIV backend is not loaded"};
    if (wrs.empty() || wrs.size() > std::numeric_limits<std::uint32_t>::max() ||
        wrs.size() > std::numeric_limits<std::size_t>::max() / sizeof(NdsSendWr)) {
        return Error{ErrorCode::kInvalidArgument, "invalid AIV transport send batch"};
    }

    NDS_ASSIGN_OR_RETURN(MemoryBuffer device_wrs,
                         runtime->allocate(wrs.size() * sizeof(NdsSendWr), MemoryLocation::Device));
    NDS_RETURN_IF_ERROR(runtime->copy_to(&device_wrs, wrs.data(), wrs.size() * sizeof(NdsSendWr)));

    NdsPostSendBatchArgs request{qp, reinterpret_cast<std::uint64_t>(device_wrs.data()),
                                 static_cast<std::uint32_t>(wrs.size()), std::numeric_limits<std::int32_t>::min(), 0U};
    NDS_ASSIGN_OR_RETURN(MemoryBuffer device_request, runtime->allocate(sizeof(request), MemoryLocation::Device));
    NDS_RETURN_IF_ERROR(runtime->copy_to(&device_request, &request, sizeof(request)));

    const std::uint64_t request_address = reinterpret_cast<std::uint64_t>(device_request.data());
    BatchArguments arguments{request_address + offsetof(NdsPostSendBatchArgs, qp), request.wrs_address,
                             request.wr_count, request_address + offsetof(NdsPostSendBatchArgs, bad_wr_address),
                             request_address + offsetof(NdsPostSendBatchArgs, return_value)};
    const int launch_result = launcher->launch("nds_aiv_post_send_batch_kernel", config, &arguments, sizeof(arguments));
    if (launch_result != ACL_SUCCESS)
        return Error{ErrorCode::kRuntime, "AIV transport batch launch failed: " + std::to_string(launch_result)};
    const int sync_result = aclrtSynchronizeStreamWithTimeout(config.stream, config.sync_timeout_ms);
    if (sync_result != ACL_SUCCESS)
        return Error{ErrorCode::kRuntime, "AIV transport batch synchronization failed: " + std::to_string(sync_result)};
    NDS_RETURN_IF_ERROR(runtime->copy_from(&request, device_request, sizeof(request)));
    std::size_t posted = wrs.size();
    if (request.return_value != NDS_OPERATION_SUCCESS) {
        std::size_t bad_index = 0U;
        if (request.bad_wr_address >= request.wrs_address) {
            const std::uint64_t delta = request.bad_wr_address - request.wrs_address;
            if (delta % sizeof(NdsSendWr) == 0U && delta / sizeof(NdsSendWr) < wrs.size())
                bad_index = static_cast<std::size_t>(delta / sizeof(NdsSendWr));
        }
        posted = bad_index;
    }
    return PostSendBatchResult{posted, request.return_value};
}

template <typename WorkRequest>
Result<void> launch_transport_and_wait(Runtime *runtime, const AivLauncher *launcher, const LaunchConfig &config,
                                       const char *entry, const NdsTransportDescriptor &transport,
                                       std::uint32_t queue_index, const WorkRequest &work_request) {
    if (runtime == nullptr || launcher == nullptr)
        return Error{ErrorCode::kRuntime, "AIV backend is not loaded"};
    if (transport.qp_descriptors_address == 0U || queue_index >= transport.qp_count)
        return Error{ErrorCode::kInvalidArgument, "AIV transport operation requires a valid QP selection"};

    struct Request {
        NdsTransportDescriptor transport;
        WorkRequest work_request;
        std::int32_t return_value;
    } request{transport, work_request, std::numeric_limits<std::int32_t>::min()};
    NDS_ASSIGN_OR_RETURN(MemoryBuffer device_request, runtime->allocate(sizeof(request), MemoryLocation::Device));
    NDS_RETURN_IF_ERROR(runtime->copy_to(&device_request, &request, sizeof(request)));
    const std::uint64_t address = reinterpret_cast<std::uint64_t>(device_request.data());
    const TransportArguments arguments{address + offsetof(Request, transport),
                                       address + offsetof(Request, work_request), queue_index, 0U,
                                       address + offsetof(Request, return_value)};
    const int launch_result =
        launcher->launch(entry, config, const_cast<TransportArguments *>(&arguments), sizeof(arguments));
    if (launch_result != ACL_SUCCESS)
        return Error{ErrorCode::kRuntime, "AIV transport launch failed: " + std::to_string(launch_result)};
    const int sync_result = aclrtSynchronizeStreamWithTimeout(config.stream, config.sync_timeout_ms);
    if (sync_result != ACL_SUCCESS)
        return Error{ErrorCode::kRuntime, "AIV transport synchronization failed: " + std::to_string(sync_result)};
    NDS_RETURN_IF_ERROR(runtime->copy_from(&request, device_request, sizeof(request)));
    return request.return_value == 0
               ? Result<void>{}
               : Error{ErrorCode::kRuntime, "AIV transport operation failed: " + std::to_string(request.return_value)};
}

Result<PostSendBatchResult> launch_transport_batch_and_wait(Runtime *runtime, const AivLauncher *launcher,
                                                            const LaunchConfig &config,
                                                            const NdsTransportDescriptor &transport,
                                                            std::uint32_t queue_index, std::span<const NdsSendWr> wrs) {
    if (runtime == nullptr || launcher == nullptr)
        return Error{ErrorCode::kRuntime, "AIV backend is not loaded"};
    if (transport.qp_descriptors_address == 0U || queue_index >= transport.qp_count)
        return Error{ErrorCode::kInvalidArgument, "AIV transport batch requires a valid QP selection"};
    if (wrs.empty() || wrs.size() > std::numeric_limits<std::uint32_t>::max() ||
        wrs.size() > std::numeric_limits<std::size_t>::max() / sizeof(NdsSendWr))
        return Error{ErrorCode::kInvalidArgument, "invalid AIV transport send batch"};

    NDS_ASSIGN_OR_RETURN(MemoryBuffer device_wrs,
                         runtime->allocate(wrs.size() * sizeof(NdsSendWr), MemoryLocation::Device));
    NDS_RETURN_IF_ERROR(runtime->copy_to(&device_wrs, wrs.data(), wrs.size() * sizeof(NdsSendWr)));
    struct Request {
        NdsTransportDescriptor transport;
        std::uint64_t wrs_address;
        std::uint32_t wr_count;
        std::int32_t return_value;
        std::uint64_t bad_wr_address;
    } request{transport, reinterpret_cast<std::uint64_t>(device_wrs.data()), static_cast<std::uint32_t>(wrs.size()),
              std::numeric_limits<std::int32_t>::min(), 0U};
    NDS_ASSIGN_OR_RETURN(MemoryBuffer device_request, runtime->allocate(sizeof(request), MemoryLocation::Device));
    NDS_RETURN_IF_ERROR(runtime->copy_to(&device_request, &request, sizeof(request)));
    const std::uint64_t address = reinterpret_cast<std::uint64_t>(device_request.data());
    const TransportBatchArguments arguments{address + offsetof(Request, transport),
                                            request.wrs_address,
                                            queue_index,
                                            request.wr_count,
                                            address + offsetof(Request, bad_wr_address),
                                            address + offsetof(Request, return_value)};
    const int launch_result = launcher->launch("nds_aiv_rdma_send_batch_kernel", config,
                                               const_cast<TransportBatchArguments *>(&arguments), sizeof(arguments));
    if (launch_result != ACL_SUCCESS)
        return Error{ErrorCode::kRuntime, "AIV transport batch launch failed: " + std::to_string(launch_result)};
    const int sync_result = aclrtSynchronizeStreamWithTimeout(config.stream, config.sync_timeout_ms);
    if (sync_result != ACL_SUCCESS)
        return Error{ErrorCode::kRuntime, "AIV transport batch synchronization failed: " + std::to_string(sync_result)};
    NDS_RETURN_IF_ERROR(runtime->copy_from(&request, device_request, sizeof(request)));
    std::size_t posted = wrs.size();
    if (request.return_value != NDS_OPERATION_SUCCESS && request.bad_wr_address >= request.wrs_address) {
        const std::uint64_t delta = request.bad_wr_address - request.wrs_address;
        if (delta % sizeof(NdsSendWr) == 0U && delta / sizeof(NdsSendWr) < wrs.size())
            posted = static_cast<std::size_t>(delta / sizeof(NdsSendWr));
    }
    return PostSendBatchResult{posted, request.return_value};
}

template <typename Request>
Result<void> launch_storage_and_wait(Runtime *runtime, const AivLauncher *launcher, const LaunchConfig &config,
                                     const char *entry, Request request) {
    if (runtime == nullptr || launcher == nullptr)
        return Error{ErrorCode::kRuntime, "AIV backend is not loaded"};
    NDS_ASSIGN_OR_RETURN(MemoryBuffer device_request, runtime->allocate(sizeof(request), MemoryLocation::Device));
    NDS_RETURN_IF_ERROR(runtime->copy_to(&device_request, &request, sizeof(request)));
    const std::uint64_t address = reinterpret_cast<std::uint64_t>(device_request.data());
    const StorageArguments arguments{address + offsetof(Request, storage), address + offsetof(Request, command),
                                     address + offsetof(Request, return_value)};
    const int launch_result =
        launcher->launch(entry, config, const_cast<StorageArguments *>(&arguments), sizeof(arguments));
    if (launch_result != ACL_SUCCESS)
        return Error{ErrorCode::kRuntime, "AIV storage launch failed: " + std::to_string(launch_result)};
    const int sync_result = aclrtSynchronizeStreamWithTimeout(config.stream, config.sync_timeout_ms);
    if (sync_result != ACL_SUCCESS)
        return Error{ErrorCode::kRuntime, "AIV storage synchronization failed: " + std::to_string(sync_result)};
    NDS_RETURN_IF_ERROR(runtime->copy_from(&request, device_request, sizeof(request)));
    return request.return_value == 0
               ? Result<void>{}
               : Error{ErrorCode::kRuntime, "AIV storage operation failed: " + std::to_string(request.return_value)};
}

}  // namespace

AivLauncher::~AivLauncher() {
    reset();
}

Result<std::unique_ptr<Launcher>> AivLauncher::open(Runtime *runtime, const std::string &kernel_path) {
    if (runtime == nullptr)
        return Error{ErrorCode::kInvalidArgument, "AIV launcher requires a runtime"};
    std::unique_ptr<AivLauncher> launcher = std::make_unique<AivLauncher>();
    launcher->runtime_ = runtime;
    NDS_RETURN_IF_ERROR(launcher->load(kernel_path));
    return std::unique_ptr<Launcher>(std::move(launcher));
}

Result<void> AivLauncher::load(const std::string &kernel_path) {
    aclrtBinaryLoadOption option{};
    aclrtBinaryLoadOptions options{};
    if (loaded() || kernel_path.empty())
        return Error{ErrorCode::kInvalidArgument, loaded() ? "NDS AIV launcher is already loaded"
                                                           : "NDS AIV requires an NDS-built kernel binary path"};

    option.type = ACL_RT_BINARY_LOAD_OPT_LAZY_LOAD;
    option.value.isLazyLoad = 1U;
    options.options = &option;
    options.numOpt = 1U;
    const int load_result = aclrtBinaryLoadFromFile(kernel_path.c_str(), &options, &binary_);
    if (load_result != ACL_SUCCESS || binary_ == nullptr) {
        reset();
        return Error{ErrorCode::kRuntime,
                     "aclrtBinaryLoadFromFile(NDS AIV binary) failed: " + std::to_string(load_result)};
    }

    return {};
}

int AivLauncher::launch(const char *kernel_name, const LaunchConfig &launch_config, void *arguments,
                        std::size_t argument_size) const {
    if (!loaded() || kernel_name == nullptr || arguments == nullptr || argument_size == 0U ||
        launch_config.stream == nullptr || launch_config.block_dim == 0U)
        return -1;

    const auto [entry, inserted] = functions_.try_emplace(kernel_name, nullptr);
    if (inserted) {
        const int result = aclrtBinaryGetFunction(binary_, kernel_name, &entry->second);
        if (result != ACL_SUCCESS || entry->second == nullptr) {
            functions_.erase(entry);
            return result;
        }
    }

    aclrtLaunchKernelAttr attributes[2]{};
    attributes[0].id = ACL_RT_LAUNCH_KERNEL_ATTR_SCHEM_MODE;
    attributes[0].value.schemMode = 1U;
    attributes[1].id = ACL_RT_LAUNCH_KERNEL_ATTR_ENGINE_TYPE;
    attributes[1].value.engineType = ACL_RT_ENGINE_TYPE_AIV;
    aclrtLaunchKernelCfg default_config{attributes, 2U};
    aclrtLaunchKernelCfg *kernel_config =
        launch_config.kernel_config == nullptr ? &default_config : launch_config.kernel_config;
    return aclrtLaunchKernelWithHostArgs(
        entry->second, launch_config.block_dim, launch_config.stream, kernel_config, arguments, argument_size,
        reinterpret_cast<aclrtPlaceHolderInfo *>(launch_config.l2ctrl), launch_config.flags);
}

Result<void> AivLauncher::launch_and_wait(const char *kernel_name, void *arguments, std::size_t argument_size,
                                          std::uint32_t timeout_ms) const {
    if (timeout_ms == 0U || timeout_ms > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()))
        return Error{ErrorCode::kInvalidArgument, "AIV launch timeout is outside the supported range"};
    aclrtStream stream{};
    const int create_result = aclrtCreateStream(&stream);
    if (create_result != ACL_SUCCESS || stream == nullptr)
        return Error{ErrorCode::kRuntime, "AIV launch stream creation failed: " + std::to_string(create_result)};
    const LaunchConfig config{.stream = stream, .sync = true, .sync_timeout_ms = static_cast<std::int32_t>(timeout_ms)};
    const int launch_result = launch(kernel_name, config, arguments, argument_size);
    if (launch_result != ACL_SUCCESS) {
        (void)aclrtDestroyStream(stream);
        return Error{ErrorCode::kRuntime, "AIV kernel launch failed: " + std::to_string(launch_result)};
    }
    const int sync_result = aclrtSynchronizeStreamWithTimeout(stream, config.sync_timeout_ms);
    (void)aclrtDestroyStream(stream);
    return sync_result == ACL_SUCCESS
               ? Result<void>{}
               : Error{ErrorCode::kRuntime, "AIV kernel synchronization failed: " + std::to_string(sync_result)};
}

void AivLauncher::reset() noexcept {
    functions_.clear();
    if (binary_ != nullptr)
        (void)aclrtBinaryUnLoad(binary_);
    binary_ = nullptr;
}

bool AivLauncher::loaded() const noexcept {
    return binary_ != nullptr;
}

Result<void> AivLauncher::post_send_with_config(const LaunchConfig &config, const NdsQpDescriptor &qp,
                                                const NdsSendWr &wr) const {
    return launch_post_and_wait(runtime_, this, config, "nds_aiv_post_send_kernel", qp, wr);
}

Result<PostSendBatchResult> AivLauncher::post_send_batch_with_config(const LaunchConfig &config,
                                                                     const NdsQpDescriptor &qp,
                                                                     std::span<const NdsSendWr> wrs) const {
    return launch_post_batch_and_wait(runtime_, this, config, qp, wrs);
}

Result<void> AivLauncher::post_recv_with_config(const LaunchConfig &config, const NdsQpDescriptor &qp,
                                                const NdsRecvWr &wr) const {
    return launch_post_and_wait(runtime_, this, config, "nds_aiv_post_recv_kernel", qp, wr);
}

Result<std::uint32_t> AivLauncher::poll_cq_with_config(const LaunchConfig &config, const NdsQpDescriptor &qp,
                                                       bool send_cq, std::uint32_t max_completions,
                                                       NdsWc *completions) const {
    if (runtime_ == nullptr || completions == nullptr || max_completions == 0U)
        return Error{ErrorCode::kInvalidArgument, "invalid AIV CQ poll arguments"};
    NDS_ASSIGN_OR_RETURN(MemoryBuffer device_completions,
                         runtime_->allocate(max_completions * sizeof(*completions), MemoryLocation::Device));
    NdsPollCqArgs request{qp, send_cq ? 1U : 0U, max_completions,
                          reinterpret_cast<std::uint64_t>(device_completions.data()),
                          std::numeric_limits<std::int32_t>::min()};
    NDS_ASSIGN_OR_RETURN(MemoryBuffer device_request, runtime_->allocate(sizeof(request), MemoryLocation::Device));
    NDS_RETURN_IF_ERROR(runtime_->copy_to(&device_request, &request, sizeof(request)));
    const std::uint64_t address = reinterpret_cast<std::uint64_t>(device_request.data());
    PollArguments arguments{address + offsetof(NdsPollCqArgs, qp), send_cq ? 1U : 0U, max_completions,
                            reinterpret_cast<std::uint64_t>(device_completions.data()),
                            address + offsetof(NdsPollCqArgs, return_value)};
    const int launch_result = this->launch("nds_aiv_poll_cq_kernel", config, &arguments, sizeof(arguments));
    if (launch_result != ACL_SUCCESS)
        return Error{ErrorCode::kRuntime, "AIV CQ poll launch failed: " + std::to_string(launch_result)};
    const int sync_result = aclrtSynchronizeStreamWithTimeout(config.stream, config.sync_timeout_ms);
    if (sync_result != ACL_SUCCESS)
        return Error{ErrorCode::kRuntime, "AIV CQ poll synchronization failed: " + std::to_string(sync_result)};
    NDS_RETURN_IF_ERROR(runtime_->copy_from(&request, device_request, sizeof(request)));
    if (request.return_value < 0)
        return Error{ErrorCode::kRuntime, "AIV CQ poll failed: " + std::to_string(request.return_value)};
    const std::uint32_t count = static_cast<std::uint32_t>(request.return_value);
    if (count != 0U)
        NDS_RETURN_IF_ERROR(runtime_->copy_from(completions, device_completions, count * sizeof(*completions)));
    return count;
}

Result<void> AivLauncher::rdma_send_with_config(const LaunchConfig &config, const NdsTransportDescriptor &transport,
                                                std::uint32_t queue_index, const NdsSendWr &wr) const {
    return launch_transport_and_wait(runtime_, this, config, "nds_aiv_rdma_send_kernel", transport, queue_index, wr);
}

Result<void> AivLauncher::rdma_recv_with_config(const LaunchConfig &config, const NdsTransportDescriptor &transport,
                                                std::uint32_t queue_index, const NdsRecvWr &wr) const {
    return launch_transport_and_wait(runtime_, this, config, "nds_aiv_rdma_recv_kernel", transport, queue_index, wr);
}

Result<void> AivLauncher::rdma_read_with_config(const LaunchConfig &config, const NdsTransportDescriptor &transport,
                                                std::uint32_t queue_index, const NdsSendWr &wr) const {
    return launch_transport_and_wait(runtime_, this, config, "nds_aiv_rdma_read_kernel", transport, queue_index, wr);
}

Result<void> AivLauncher::rdma_write_with_config(const LaunchConfig &config, const NdsTransportDescriptor &transport,
                                                 std::uint32_t queue_index, const NdsSendWr &wr) const {
    return launch_transport_and_wait(runtime_, this, config, "nds_aiv_rdma_write_kernel", transport, queue_index, wr);
}

Result<PostSendBatchResult> AivLauncher::rdma_send_batch_with_config(const LaunchConfig &config,
                                                                     const NdsTransportDescriptor &transport,
                                                                     std::uint32_t queue_index,
                                                                     std::span<const NdsSendWr> wrs) const {
    return launch_transport_batch_and_wait(runtime_, this, config, transport, queue_index, wrs);
}

Result<void> AivLauncher::storage_read_with_config(const LaunchConfig &config, const NdsStorageDescriptor &storage,
                                                   const nds::StorageReadCommand &command) const {
    const NdsStorageReadArgs request{
        .storage = storage, .command = command, .return_value = std::numeric_limits<std::int32_t>::min()};
    return launch_storage_and_wait(runtime_, this, config, "nds_aiv_storage_read_kernel", request);
}

Result<void> AivLauncher::storage_write_with_config(const LaunchConfig &config, const NdsStorageDescriptor &storage,
                                                    const nds::StorageWriteCommand &command) const {
    const NdsStorageWriteArgs request{
        .storage = storage, .command = command, .return_value = std::numeric_limits<std::int32_t>::min()};
    return launch_storage_and_wait(runtime_, this, config, "nds_aiv_storage_write_kernel", request);
}

Result<void> AivLauncher::storage_read_batch_with_config(const LaunchConfig &config,
                                                         const NdsStorageDescriptor &storage,
                                                         const nds::StorageBatchReadCommand &command) const {
    const NdsStorageBatchReadArgs request{
        .storage = storage, .command = command, .return_value = std::numeric_limits<std::int32_t>::min()};
    return launch_storage_and_wait(runtime_, this, config, "nds_aiv_storage_batch_read_kernel", request);
}

Result<void> AivLauncher::storage_write_batch_with_config(const LaunchConfig &config,
                                                          const NdsStorageDescriptor &storage,
                                                          const nds::StorageBatchWriteCommand &command) const {
    const NdsStorageBatchWriteArgs request{
        .storage = storage, .command = command, .return_value = std::numeric_limits<std::int32_t>::min()};
    return launch_storage_and_wait(runtime_, this, config, "nds_aiv_storage_batch_write_kernel", request);
}

}  // namespace nds::client

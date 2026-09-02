#include "launcher.hh"

#include "launch_args.hh"
#include "runtime.hh"

#include <limits>

namespace nds::client {

namespace {

template <typename Request>
Result<void> launch_device_and_wait(Runtime *runtime, const AicpuLauncher *launcher, const LaunchConfig &config,
                                    const char *entry, Request request) {
    if (runtime == nullptr || launcher == nullptr)
        return Error{ErrorCode::kRuntime, "AICPU backend is not loaded"};
    std::int32_t return_value = std::numeric_limits<std::int32_t>::min();
    NDS_ASSIGN_OR_RETURN(MemoryBuffer device_return_value,
                         runtime->allocate(sizeof(return_value), MemoryLocation::Device));
    NDS_RETURN_IF_ERROR(runtime->copy_to(&device_return_value, &return_value, sizeof(return_value)));
    NdsAicpuLaunchArgs<Request> arguments{request, reinterpret_cast<std::uint64_t>(device_return_value.data())};
    const int launch_result = launcher->launch(entry, config, &arguments, sizeof(arguments));
    if (launch_result != ACL_SUCCESS)
        return Error{ErrorCode::kRuntime, "AICPU kernel launch failed: " + std::to_string(launch_result)};
    const int sync_result = aclrtSynchronizeStreamWithTimeout(config.stream, config.sync_timeout_ms);
    if (sync_result != ACL_SUCCESS)
        return Error{ErrorCode::kRuntime, "AICPU kernel synchronization failed: " + std::to_string(sync_result)};
    NDS_RETURN_IF_ERROR(runtime->copy_from(&return_value, device_return_value, sizeof(return_value)));
    return return_value == 0 ? Result<void>{}
                             : Error{ErrorCode::kRuntime, "AICPU device operation failed (" + std::string(entry) +
                                                              "): " + std::to_string(return_value)};
}

}  // namespace

AicpuLauncher::~AicpuLauncher() {
    reset();
}

Result<std::unique_ptr<Launcher>> AicpuLauncher::open(Runtime *runtime, const std::string &kernel_path) {
    if (runtime == nullptr)
        return Error{ErrorCode::kInvalidArgument, "AICPU launcher requires a runtime"};
    std::unique_ptr<AicpuLauncher> launcher = std::make_unique<AicpuLauncher>();
    launcher->runtime_ = runtime;
    NDS_RETURN_IF_ERROR(launcher->load(kernel_path));
    return std::unique_ptr<Launcher>(std::move(launcher));
}

Result<void> AicpuLauncher::load(const std::string &kernel_path) {
    aclrtBinaryLoadOption option{};
    aclrtBinaryLoadOptions options{};
    if (loaded() || kernel_path.empty())
        return Error{ErrorCode::kInvalidArgument, loaded() ? "NDS AICPU launcher is already loaded"
                                                           : "NDS AICPU requires an NDS kernel artifact path"};

    option.type = ACL_RT_BINARY_LOAD_OPT_CPU_KERNEL_MODE;
    option.value.cpuKernelMode = 0;
    options.options = &option;
    options.numOpt = 1U;
    const int load_result = aclrtBinaryLoadFromFile(kernel_path.c_str(), &options, &binary_);
    if (load_result != ACL_SUCCESS || binary_ == nullptr) {
        reset();
        return Error{ErrorCode::kRuntime,
                     "aclrtBinaryLoadFromFile(AICPU kernel artifact) failed: " + std::to_string(load_result)};
    }

    return {};
}

int AicpuLauncher::launch(const char *kernel_name, const LaunchConfig &launch_config, void *arguments,
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

    aclrtLaunchKernelAttr default_attribute{};
    default_attribute.id = ACL_RT_LAUNCH_KERNEL_ATTR_TIMEOUT;
    default_attribute.value.timeout = 5U;
    aclrtLaunchKernelCfg default_config{&default_attribute, 1U};
    aclrtLaunchKernelCfg *kernel_config =
        launch_config.kernel_config == nullptr ? &default_config : launch_config.kernel_config;
    return aclrtLaunchKernelWithHostArgs(
        entry->second, launch_config.block_dim, launch_config.stream, kernel_config, arguments, argument_size,
        reinterpret_cast<aclrtPlaceHolderInfo *>(launch_config.l2ctrl), launch_config.flags);
}

Result<void> AicpuLauncher::launch_and_wait(const char *kernel_name, void *arguments, std::size_t argument_size,
                                            std::uint32_t timeout_ms) const {
    if (timeout_ms == 0U || timeout_ms > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()))
        return Error{ErrorCode::kInvalidArgument, "AICPU launch timeout is outside the supported range"};
    aclrtStream stream{};
    const int create_result = aclrtCreateStream(&stream);
    if (create_result != ACL_SUCCESS || stream == nullptr)
        return Error{ErrorCode::kRuntime, "AICPU launch stream creation failed: " + std::to_string(create_result)};
    const LaunchConfig config{.stream = stream, .sync = true, .sync_timeout_ms = static_cast<std::int32_t>(timeout_ms)};
    const int launch_result = launch(kernel_name, config, arguments, argument_size);
    if (launch_result != ACL_SUCCESS) {
        (void)aclrtDestroyStream(stream);
        return Error{ErrorCode::kRuntime, "AICPU kernel launch failed: " + std::to_string(launch_result)};
    }
    const int sync_result = aclrtSynchronizeStreamWithTimeout(stream, config.sync_timeout_ms);
    (void)aclrtDestroyStream(stream);
    return sync_result == ACL_SUCCESS
               ? Result<void>{}
               : Error{ErrorCode::kRuntime, "AICPU kernel synchronization failed: " + std::to_string(sync_result)};
}

void AicpuLauncher::reset() noexcept {
    functions_.clear();
    if (binary_ != nullptr)
        (void)aclrtBinaryUnLoad(binary_);
    binary_ = nullptr;
}

bool AicpuLauncher::loaded() const noexcept {
    return binary_ != nullptr;
}

Result<void> AicpuLauncher::post_send_with_config(const LaunchConfig &config, const NdsQpDescriptor &qp,
                                                  const NdsSendWr &wr) const {
    const NdsPostSendArgs request{qp, wr, std::numeric_limits<std::int32_t>::min()};
    return launch_device_and_wait(runtime_, this, config, "nds_aicpu_post_send_kernel", request);
}

Result<void> AicpuLauncher::post_recv_with_config(const LaunchConfig &config, const NdsQpDescriptor &qp,
                                                  const NdsRecvWr &wr) const {
    const NdsPostRecvArgs request{qp, wr, std::numeric_limits<std::int32_t>::min()};
    return launch_device_and_wait(runtime_, this, config, "nds_aicpu_post_recv_kernel", request);
}

Result<std::uint32_t> AicpuLauncher::poll_cq_with_config(const LaunchConfig &config, const NdsQpDescriptor &qp,
                                                         bool send_cq, std::uint32_t max_completions,
                                                         NdsWc *completions) const {
    if (runtime_ == nullptr || completions == nullptr || max_completions == 0U)
        return Error{ErrorCode::kInvalidArgument, "invalid AICPU CQ poll arguments"};
    NDS_ASSIGN_OR_RETURN(MemoryBuffer device_completions,
                         runtime_->allocate(max_completions * sizeof(*completions), MemoryLocation::Device));
    NdsPollCqArgs request{qp, send_cq ? 1U : 0U, max_completions,
                          reinterpret_cast<std::uint64_t>(device_completions.data()),
                          std::numeric_limits<std::int32_t>::min()};
    std::int32_t return_value = std::numeric_limits<std::int32_t>::min();
    NDS_ASSIGN_OR_RETURN(MemoryBuffer device_return_value,
                         runtime_->allocate(sizeof(return_value), MemoryLocation::Device));
    NDS_RETURN_IF_ERROR(runtime_->copy_to(&device_return_value, &return_value, sizeof(return_value)));
    NdsAicpuLaunchArgs<NdsPollCqArgs> arguments{request, reinterpret_cast<std::uint64_t>(device_return_value.data())};
    const int launch_result = this->launch("nds_aicpu_poll_cq_kernel", config, &arguments, sizeof(arguments));
    if (launch_result != ACL_SUCCESS)
        return Error{ErrorCode::kRuntime, "AICPU CQ poll launch failed: " + std::to_string(launch_result)};
    const int sync_result = aclrtSynchronizeStreamWithTimeout(config.stream, config.sync_timeout_ms);
    if (sync_result != ACL_SUCCESS)
        return Error{ErrorCode::kRuntime, "AICPU CQ poll synchronization failed: " + std::to_string(sync_result)};
    NDS_RETURN_IF_ERROR(runtime_->copy_from(&return_value, device_return_value, sizeof(return_value)));
    if (return_value < 0)
        return Error{ErrorCode::kRuntime, "AICPU CQ poll failed: " + std::to_string(return_value)};
    const std::uint32_t count = static_cast<std::uint32_t>(return_value);
    if (count != 0U)
        NDS_RETURN_IF_ERROR(runtime_->copy_from(completions, device_completions, count * sizeof(*completions)));
    return count;
}

Result<void> AicpuLauncher::rdma_send_with_config(const LaunchConfig &config, const NdsTransportDescriptor &transport,
                                                  std::uint32_t queue_index, const NdsSendWr &wr) const {
    const NdsRdmaSendArgs request{transport, wr, queue_index, std::numeric_limits<std::int32_t>::min()};
    return launch_device_and_wait(runtime_, this, config, "nds_aicpu_rdma_send_kernel", request);
}

Result<void> AicpuLauncher::rdma_recv_with_config(const LaunchConfig &config, const NdsTransportDescriptor &transport,
                                                  std::uint32_t queue_index, const NdsRecvWr &wr) const {
    const NdsRdmaRecvArgs request{transport, wr, queue_index, std::numeric_limits<std::int32_t>::min()};
    return launch_device_and_wait(runtime_, this, config, "nds_aicpu_rdma_recv_kernel", request);
}

Result<void> AicpuLauncher::rdma_read_with_config(const LaunchConfig &config, const NdsTransportDescriptor &transport,
                                                  std::uint32_t queue_index, const NdsSendWr &wr) const {
    const NdsRdmaReadArgs request{transport, wr, queue_index, std::numeric_limits<std::int32_t>::min()};
    return launch_device_and_wait(runtime_, this, config, "nds_aicpu_rdma_read_kernel", request);
}

Result<void> AicpuLauncher::rdma_write_with_config(const LaunchConfig &config, const NdsTransportDescriptor &transport,
                                                   std::uint32_t queue_index, const NdsSendWr &wr) const {
    const NdsRdmaWriteArgs request{transport, wr, queue_index, std::numeric_limits<std::int32_t>::min()};
    return launch_device_and_wait(runtime_, this, config, "nds_aicpu_rdma_write_kernel", request);
}

Result<void> AicpuLauncher::storage_read_with_config(const LaunchConfig &config, const NdsStorageDescriptor &storage,
                                                     const nds::StorageReadCommand &command) const {
    const NdsStorageReadArgs request{
        .storage = storage, .command = command, .return_value = std::numeric_limits<std::int32_t>::min()};
    return launch_device_and_wait(runtime_, this, config, "nds_aicpu_storage_read_kernel", request);
}

Result<void> AicpuLauncher::storage_write_with_config(const LaunchConfig &config, const NdsStorageDescriptor &storage,
                                                      const nds::StorageWriteCommand &command) const {
    const NdsStorageWriteArgs request{
        .storage = storage, .command = command, .return_value = std::numeric_limits<std::int32_t>::min()};
    return launch_device_and_wait(runtime_, this, config, "nds_aicpu_storage_write_kernel", request);
}

Result<void> AicpuLauncher::storage_read_batch_with_config(const LaunchConfig &config,
                                                           const NdsStorageDescriptor &storage,
                                                           const nds::StorageBatchReadCommand &command) const {
    const NdsStorageBatchReadArgs request{
        .storage = storage, .command = command, .return_value = std::numeric_limits<std::int32_t>::min()};
    return launch_device_and_wait(runtime_, this, config, "nds_aicpu_storage_batch_read_kernel", request);
}

Result<void> AicpuLauncher::storage_write_batch_with_config(const LaunchConfig &config,
                                                            const NdsStorageDescriptor &storage,
                                                            const nds::StorageBatchWriteCommand &command) const {
    const NdsStorageBatchWriteArgs request{
        .storage = storage, .command = command, .return_value = std::numeric_limits<std::int32_t>::min()};
    return launch_device_and_wait(runtime_, this, config, "nds_aicpu_storage_batch_write_kernel", request);
}

}  // namespace nds::client

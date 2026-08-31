#include "launcher.hh"

#include "runtime.hh"

#include <cstddef>
#include <limits>

namespace nds::client {

namespace {

template <typename WorkRequest>
struct PostArguments {
    NdsDeviceQp qp;
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

template <typename WorkRequest>
Result<void> launch_post_and_wait(Runtime *runtime, const AivLauncher *launcher, const LaunchConfig &config,
                                  const char *entry, const NdsDeviceQp &qp, const WorkRequest &work_request) {
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

void AivLauncher::reset() noexcept {
    functions_.clear();
    if (binary_ != nullptr)
        (void)aclrtBinaryUnLoad(binary_);
    binary_ = nullptr;
}

bool AivLauncher::loaded() const noexcept {
    return binary_ != nullptr;
}

Result<void> AivLauncher::post_send_with_config(const LaunchConfig &config, const NdsDeviceQp &qp,
                                                const NdsDeviceSendWr &wr) const {
    return launch_post_and_wait(runtime_, this, config, "nds_aiv_post_send_kernel", qp, wr);
}

Result<void> AivLauncher::post_recv_with_config(const LaunchConfig &config, const NdsDeviceQp &qp,
                                                const NdsDeviceRecvWr &wr) const {
    return launch_post_and_wait(runtime_, this, config, "nds_aiv_post_recv_kernel", qp, wr);
}

Result<std::uint32_t> AivLauncher::poll_cq_with_config(const LaunchConfig &config, const NdsDeviceQp &qp, bool send_cq,
                                                       std::uint32_t max_completions, NdsDeviceWc *completions) const {
    if (runtime_ == nullptr || completions == nullptr || max_completions == 0U)
        return Error{ErrorCode::kInvalidArgument, "invalid AIV CQ poll arguments"};
    NDS_ASSIGN_OR_RETURN(MemoryBuffer device_completions,
                         runtime_->allocate(max_completions * sizeof(*completions), MemoryLocation::Device));
    NdsDevicePollCqArgs request{qp, send_cq ? 1U : 0U, max_completions,
                                reinterpret_cast<std::uint64_t>(device_completions.data()),
                                std::numeric_limits<std::int32_t>::min()};
    NDS_ASSIGN_OR_RETURN(MemoryBuffer device_request, runtime_->allocate(sizeof(request), MemoryLocation::Device));
    NDS_RETURN_IF_ERROR(runtime_->copy_to(&device_request, &request, sizeof(request)));
    const std::uint64_t address = reinterpret_cast<std::uint64_t>(device_request.data());
    PollArguments arguments{address + offsetof(NdsDevicePollCqArgs, qp), send_cq ? 1U : 0U, max_completions,
                            reinterpret_cast<std::uint64_t>(device_completions.data()),
                            address + offsetof(NdsDevicePollCqArgs, return_value)};
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

}  // namespace nds::client

#include "launcher.hh"

namespace nds {

AicpuLauncher::~AicpuLauncher() {
    reset();
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

int AicpuLauncher::launch(const char *kernel_name, const client::LaunchConfig &launch_config, void *arguments,
                          std::size_t argument_size) {
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

void AicpuLauncher::reset() noexcept {
    functions_.clear();
    if (binary_ != nullptr)
        (void)aclrtBinaryUnLoad(binary_);
    binary_ = nullptr;
}

bool AicpuLauncher::loaded() const noexcept {
    return binary_ != nullptr;
}

}  // namespace nds

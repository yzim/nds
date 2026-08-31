#include "launcher.hh"

namespace nds {

AivLauncher::~AivLauncher() {
    reset();
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

int AivLauncher::launch(const char *kernel_name, const client::LaunchConfig &launch_config, void *arguments,
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

}  // namespace nds

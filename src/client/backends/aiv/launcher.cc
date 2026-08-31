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

    const int stream_result = aclrtCreateStream(&stream_);
    if (stream_result != ACL_SUCCESS || stream_ == nullptr) {
        reset();
        return Error{ErrorCode::kRuntime,
                     "aclrtCreateStream for NDS AIV launch failed: " + std::to_string(stream_result)};
    }
    return {};
}

Result<void> AivLauncher::launch(const char *kernel_name, void *arguments, std::size_t argument_size) {
    if (!loaded() || kernel_name == nullptr || arguments == nullptr || argument_size == 0U)
        return Error{ErrorCode::kInvalidArgument, "invalid AIV launch arguments"};

    const auto [entry, inserted] = functions_.try_emplace(kernel_name, nullptr);
    if (inserted) {
        const int result = aclrtBinaryGetFunction(binary_, kernel_name, &entry->second);
        if (result != ACL_SUCCESS || entry->second == nullptr) {
            functions_.erase(entry);
            return Error{ErrorCode::kRuntime, "AIV kernel entry lookup failed: " + std::string(kernel_name)};
        }
    }

    aclrtLaunchKernelAttr attributes[2]{};
    attributes[0].id = ACL_RT_LAUNCH_KERNEL_ATTR_SCHEM_MODE;
    attributes[0].value.schemMode = 1U;
    attributes[1].id = ACL_RT_LAUNCH_KERNEL_ATTR_ENGINE_TYPE;
    attributes[1].value.engineType = ACL_RT_ENGINE_TYPE_AIV;
    aclrtLaunchKernelCfg config{attributes, 2U};
    const int result =
        aclrtLaunchKernelWithHostArgs(entry->second, 1U, stream_, &config, arguments, argument_size, nullptr, 0U);
    return result == ACL_SUCCESS ? Result<void>{}
                                 : Error{ErrorCode::kRuntime, "AIV kernel launch failed: " + std::string(kernel_name)};
}

Result<void> AivLauncher::synchronize(std::int32_t completion_timeout_ms) {
    if (!loaded() || completion_timeout_ms <= 0)
        return Error{ErrorCode::kInvalidArgument, "invalid AIV synchronization timeout"};
    const int result = aclrtSynchronizeStreamWithTimeout(stream_, completion_timeout_ms);
    return result == ACL_SUCCESS ? Result<void>{} : Error{ErrorCode::kRuntime, "AIV kernel synchronization failed"};
}

Result<void> AivLauncher::launch_and_wait(const char *kernel_name, void *arguments, std::size_t argument_size,
                                          std::int32_t completion_timeout_ms) {
    if (const auto submitted = launch(kernel_name, arguments, argument_size); !submitted)
        return Error{submitted.error()};
    return synchronize(completion_timeout_ms);
}

void AivLauncher::reset() noexcept {
    if (stream_ != nullptr)
        (void)aclrtDestroyStream(stream_);
    stream_ = nullptr;
    functions_.clear();
    if (binary_ != nullptr)
        (void)aclrtBinaryUnLoad(binary_);
    binary_ = nullptr;
}

bool AivLauncher::loaded() const noexcept {
    return binary_ != nullptr && stream_ != nullptr;
}

}  // namespace nds

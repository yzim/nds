#include "launcher.hh"

#include <utility>

namespace nds {

AivLauncher::~AivLauncher() {
    reset();
}

Result<void> AivLauncher::load(NdsAclApi *acl, const std::string &kernel_path) {
    NdsAclBinaryLoadOption option{};
    NdsAclBinaryLoadOptions options{};

    if (acl == nullptr || loaded() || kernel_path.empty()) {
        return unexpected(ErrorCode::kInvalidArgument, loaded() ? "NDS AIV launcher is already loaded"
                                                                : "NDS AIV requires an NDS-built kernel binary path");
    }
    if (acl->binary_load_from_file == nullptr || acl->binary_unload == nullptr || acl->binary_get_function == nullptr ||
        acl->launch_kernel_with_host_args == nullptr || acl->create_stream == nullptr ||
        acl->destroy_stream == nullptr || acl->synchronize_stream_with_timeout == nullptr) {
        return unexpected(ErrorCode::kRuntime,
                          "AscendCL is missing a required AIV binary, host-argument launch, or stream symbol");
    }

    acl_ = acl;
    option.type = NDS_ACL_BINARY_LOAD_OPT_LAZY_LOAD;
    option.value.lazy_load = 1U;
    options.options = &option;
    options.num_options = 1U;
    const int load_result = acl_->binary_load_from_file(kernel_path.c_str(), &options, &binary_);
    if (load_result != 0 || binary_ == nullptr) {
        const std::string error = "aclrtBinaryLoadFromFile(NDS AIV binary) failed: " + std::to_string(load_result);
        reset();
        return unexpected(ErrorCode::kRuntime, error);
    }
    const int stream_result = acl_->create_stream(&stream_);
    if (stream_result != 0 || stream_ == nullptr) {
        const std::string error = "aclrtCreateStream for NDS AIV launch failed: " + std::to_string(stream_result);
        reset();
        return unexpected(ErrorCode::kRuntime, error);
    }
    return {};
}

Result<void> AivLauncher::launch_and_wait(const char *kernel_name, void *arguments, std::size_t argument_size,
                                          std::int32_t completion_timeout_ms) {
    if (!loaded() || kernel_name == nullptr || arguments == nullptr || argument_size == 0U ||
        completion_timeout_ms <= 0) {
        return unexpected(ErrorCode::kInvalidArgument,
                          "NDS AIV launch requires a loaded binary, kernel name, arguments, and positive timeout");
    }

    const auto [entry, inserted] = functions_.try_emplace(kernel_name, nullptr);
    if (inserted) {
        const int function_result = acl_->binary_get_function(binary_, kernel_name, &entry->second);
        if (function_result != 0 || entry->second == nullptr) {
            functions_.erase(entry);
            return unexpected(ErrorCode::kRuntime, "NDS AIV binary does not expose " + std::string(kernel_name) + ": " +
                                                       std::to_string(function_result));
        }
    }

    NdsAclLaunchKernelAttr attributes[2]{};
    attributes[0].id = NDS_ACL_LAUNCH_KERNEL_ATTR_SCHEM_MODE;
    attributes[0].value.schem_mode = 1U;
    attributes[1].id = NDS_ACL_LAUNCH_KERNEL_ATTR_ENGINE_TYPE;
    attributes[1].value.engine_type = NDS_ACL_ENGINE_TYPE_AIV;
    NdsAclLaunchKernelConfig config{attributes, 2U};
    const int launch_result =
        acl_->launch_kernel_with_host_args(entry->second, 1U, stream_, &config, arguments, argument_size, nullptr, 0U);
    if (launch_result != 0) {
        return unexpected(ErrorCode::kRuntime, "aclrtLaunchKernelWithHostArgs(" + std::string(kernel_name) +
                                                   ") failed: " + std::to_string(launch_result));
    }
    const int sync_result = acl_->synchronize_stream_with_timeout(stream_, completion_timeout_ms);
    if (sync_result != 0) {
        return unexpected(ErrorCode::kRuntime, "aclrtSynchronizeStreamWithTimeout after " + std::string(kernel_name) +
                                                   " failed: " + std::to_string(sync_result));
    }
    return {};
}

void AivLauncher::reset() noexcept {
    if (acl_ != nullptr && stream_ != nullptr && acl_->destroy_stream != nullptr)
        (void)acl_->destroy_stream(stream_);
    stream_ = nullptr;
    functions_.clear();
    if (acl_ != nullptr && binary_ != nullptr && acl_->binary_unload != nullptr)
        (void)acl_->binary_unload(binary_);
    binary_ = nullptr;
    acl_ = nullptr;
}

bool AivLauncher::loaded() const noexcept {
    return acl_ != nullptr && binary_ != nullptr && stream_ != nullptr;
}

}  // namespace nds

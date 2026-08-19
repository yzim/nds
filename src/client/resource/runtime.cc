#include "runtime.hh"

#include <utility>

namespace nds::client {

Runtime::~Runtime() {
    reset();
}

void Runtime::set_error(std::string message) {
    error_ = std::move(message);
}

Result<void> Runtime::open(const RuntimeConfig &config) {
    if (initialized_)
        return unexpected(ErrorCode::kInvalidArgument, "NPU runtime is already open");
    if (!initialize(config))
        return unexpected(ErrorCode::kRuntime, error_);
    memory_.attach(this);
    return {};
}

bool Runtime::initialize(const RuntimeConfig &config) {
    const std::string hdc_type_argument = "--hdcType=" + std::to_string(config.hdc_type);
    nds_rt_proc_ext_param parameter{};
    nds_rt_net_service_open_args open_args{};

    if (config.ascendcl_library.empty() || config.runtime_library.empty()) {
        set_error("NPU runtime requires explicit AscendCL and runtime library paths");
        return false;
    }
    config_ = config;
    parameter.param_info = hdc_type_argument.c_str();
    parameter.param_len = hdc_type_argument.size();
    open_args.ext_param_list = &parameter;
    open_args.ext_param_count = 1U;
    if (nds_acl_open(&acl_, config_.ascendcl_library.c_str()) != 0) {
        set_error(std::string("cannot load AscendCL: ") + nds_acl_error(&acl_));
        reset();
        return false;
    }
    if (const int result = acl_.init(nullptr); result != 0) {
        set_error("aclInit failed: " + std::to_string(result));
        reset();
        return false;
    }
    acl_initialized_ = true;
    if (const int result = acl_.set_device(static_cast<std::int32_t>(config_.logical_device_id)); result != 0) {
        set_error("aclrtSetDevice failed: " + std::to_string(result));
        reset();
        return false;
    }
    if (nds_runtime_open(&runtime_, config_.runtime_library.c_str()) != 0) {
        set_error(std::string("cannot load CANN runtime: ") + nds_runtime_error(&runtime_));
        reset();
        return false;
    }
    if (const int result = runtime_.open_net_service(&open_args); result != 0) {
        set_error("rtOpenNetService failed: " + std::to_string(result));
        reset();
        return false;
    }
    net_service_open_ = true;
    initialized_ = true;
    error_.clear();
    return true;
}

void Runtime::reset() noexcept {
    if (net_service_open_) {
        (void)runtime_.close_net_service();
        net_service_open_ = false;
    }
    nds_runtime_close(&runtime_);
    if (acl_initialized_) {
        (void)acl_.finalize();
        acl_initialized_ = false;
    }
    nds_acl_close(&acl_);
    initialized_ = false;
}

Memory *Runtime::memory() noexcept {
    return &memory_;
}

const RuntimeConfig &Runtime::config() const noexcept {
    return config_;
}

bool Runtime::initialized() const noexcept {
    return initialized_;
}

Result<void> Runtime::allocate_device_memory(std::size_t size, void **device_ptr) {
    if (!initialized_ || device_ptr == nullptr || size == 0U) {
        set_error("device allocation requires an initialized runtime, nonzero size, and output pointer");
        return unexpected(ErrorCode::kInvalidArgument, error_);
    }
    *device_ptr = nullptr;
    const int result = acl_.malloc_device(device_ptr, size, NDS_ACL_MEM_MALLOC_DIRECT_NPU);
    if (result != 0 || *device_ptr == nullptr) {
        set_error("aclrtMalloc failed: " + std::to_string(result));
        return unexpected(ErrorCode::kRuntime, error_);
    }
    error_.clear();
    return {};
}

Result<void> Runtime::free_device_memory(void *device_ptr) {
    if (!initialized_ || device_ptr == nullptr) {
        set_error("device free requires an initialized runtime and allocation pointer");
        return unexpected(ErrorCode::kInvalidArgument, error_);
    }
    const int result = acl_.free_device(device_ptr);
    if (result != 0) {
        set_error("aclrtFree failed: " + std::to_string(result));
        return unexpected(ErrorCode::kRuntime, error_);
    }
    error_.clear();
    return {};
}

Result<void> Runtime::copy_host_to_device(void *device_ptr, const void *host_ptr, std::size_t size) {
    const int result =
        (!initialized_ || device_ptr == nullptr || host_ptr == nullptr || size == 0U || acl_.memcpy == nullptr)
            ? -1
            : acl_.memcpy(device_ptr, size, host_ptr, size, NDS_ACL_MEMCPY_HOST_TO_DEVICE);
    if (result != 0) {
        set_error("aclrtMemcpy(host-to-device) failed: " + std::to_string(result));
        return unexpected(ErrorCode::kRuntime, error_);
    }
    error_.clear();
    return {};
}

Result<void> Runtime::copy_device_to_host(void *host_ptr, const void *device_ptr, std::size_t size) {
    const int result =
        (!initialized_ || device_ptr == nullptr || host_ptr == nullptr || size == 0U || acl_.memcpy == nullptr)
            ? -1
            : acl_.memcpy(host_ptr, size, device_ptr, size, NDS_ACL_MEMCPY_DEVICE_TO_HOST);
    if (result != 0) {
        set_error("aclrtMemcpy(device-to-host) failed: " + std::to_string(result));
        return unexpected(ErrorCode::kRuntime, error_);
    }
    error_.clear();
    return {};
}

nds_acl_api &Runtime::acl_api() noexcept {
    return acl_;
}

nds_runtime_api &Runtime::runtime_api() noexcept {
    return runtime_;
}

const std::string &Runtime::error() const noexcept {
    return error_;
}

}  // namespace nds::client

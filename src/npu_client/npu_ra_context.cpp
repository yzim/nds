#include "nds/npu_ra_context.hpp"

#include <utility>

namespace nds {

NpuRaContext::~NpuRaContext()
{
    reset();
}

void NpuRaContext::set_error(std::string message)
{
    error_ = std::move(message);
}

bool NpuRaContext::initialize(const NpuRaContextConfig &config)
{
    const std::string hdc_type_argument = "--hdcType=" + std::to_string(config.hdc_type);
    nds_rt_proc_ext_param parameter{};
    nds_rt_net_service_open_args open_args{};
    nds_ra_init_config ra_init_config{};
    int result;

    if (initialized_) {
        set_error("direct NPU RA context is already initialized");
        return false;
    }
    if (config.ascendcl_library.empty() || config.runtime_library.empty() || config.ra_library.empty()) {
        set_error("direct NPU RA context requires explicit AscendCL, runtime, and RA library paths");
        return false;
    }

    config_ = config;
    parameter.param_info = hdc_type_argument.c_str();
    parameter.param_len = hdc_type_argument.size();
    open_args.ext_param_list = &parameter;
    open_args.ext_param_count = 1U;
    ra_init_config.phy_id = config_.physical_device_id;
    ra_init_config.nic_position = NDS_RA_NETWORK_OFFLINE;
    ra_init_config.hdc_type = config_.hdc_type;
    ra_init_config.enable_hdc_async = false;

    if (nds_acl_open(&acl_, config_.ascendcl_library.c_str()) != 0) {
        set_error(std::string("cannot load AscendCL: ") + nds_acl_error(&acl_));
        goto fail;
    }
    result = acl_.init(nullptr);
    if (result != 0) {
        set_error("aclInit failed: " + std::to_string(result));
        goto fail;
    }
    acl_initialized_ = true;
    result = acl_.set_device(static_cast<std::int32_t>(config_.logical_device_id));
    if (result != 0) {
        set_error("aclrtSetDevice failed: " + std::to_string(result));
        goto fail;
    }

    if (nds_runtime_open(&runtime_, config_.runtime_library.c_str()) != 0) {
        set_error(std::string("cannot load CANN runtime: ") + nds_runtime_error(&runtime_));
        goto fail;
    }
    result = runtime_.open_net_service(&open_args);
    if (result != 0) {
        set_error("rtOpenNetService failed: " + std::to_string(result));
        goto fail;
    }
    net_service_open_ = true;

    if (nds_ra_open(&ra_, config_.ra_library.c_str()) != 0) {
        set_error(std::string("cannot load libra.so: ") + nds_ra_error(&ra_));
        goto fail;
    }
    result = ra_.ra_init(&ra_init_config);
    if (result != 0) {
        set_error("RaInit failed: " + std::to_string(result));
        goto fail;
    }
    ra_initialized_ = true;
    initialized_ = true;
    error_.clear();
    return true;

fail:
    reset();
    return false;
}

void NpuRaContext::reset() noexcept
{
    nds_ra_init_config ra_init_config{};

    ra_init_config.phy_id = config_.physical_device_id;
    ra_init_config.nic_position = NDS_RA_NETWORK_OFFLINE;
    ra_init_config.hdc_type = config_.hdc_type;
    ra_init_config.enable_hdc_async = false;
    if (ra_initialized_) {
        (void)ra_.ra_deinit(&ra_init_config);
        ra_initialized_ = false;
    }
    nds_ra_close(&ra_);
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

bool NpuRaContext::initialized() const noexcept
{
    return initialized_;
}

nds_ra_api &NpuRaContext::ra_api() noexcept
{
    return ra_;
}

const std::string &NpuRaContext::error() const noexcept
{
    return error_;
}

} // namespace nds

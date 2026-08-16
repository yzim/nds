#ifndef NDS_AICPU_LAUNCHER_HH
#define NDS_AICPU_LAUNCHER_HH

#include "nds/acl_loader.h"
#include "nds/device_operations.h"

#include <cstdint>
#include <string>

namespace nds {

class AicpuConnectionLauncher {
public:
    AicpuConnectionLauncher() = default;
    ~AicpuConnectionLauncher();
    AicpuConnectionLauncher(const AicpuConnectionLauncher &) = delete;
    AicpuConnectionLauncher &operator=(const AicpuConnectionLauncher &) = delete;

    bool load(nds_acl_api *acl, const std::string &kernel_config_path);
    bool launch_and_wait(nds_device_operation_request *request,
                         std::int32_t completion_timeout_ms);
    void reset() noexcept;
    bool loaded() const noexcept;
    const std::string &error() const noexcept;

private:
    void set_error(std::string message);

    nds_acl_api *acl_{};
    nds_acl_bin_handle binary_{};
    nds_acl_func_handle function_{};
    nds_acl_stream stream_{};
    std::string error_;
};

}  // namespace nds

#endif

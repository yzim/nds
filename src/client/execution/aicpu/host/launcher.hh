#ifndef NDS_AICPU_LAUNCHER_HH
#define NDS_AICPU_LAUNCHER_HH

#include "nds/acl_loader.h"
#include "nds/device_operator_args.h"
#include "nds/device_operations.h"
#include "nds/device_storage.h"

#include <cstdint>
#include <string>

namespace nds {

class AicpuEntrypointLauncher {
public:
    AicpuEntrypointLauncher() = default;
    ~AicpuEntrypointLauncher();
    AicpuEntrypointLauncher(const AicpuEntrypointLauncher &) = delete;
    AicpuEntrypointLauncher &operator=(const AicpuEntrypointLauncher &) = delete;

    bool load(nds_acl_api *acl, const std::string &kernel_path);
    bool launch_and_wait(nds_device_operation_request *request, std::int32_t completion_timeout_ms);
    bool launch_post_send_and_wait(nds_device_post_send_request *request, std::int32_t completion_timeout_ms);
    bool launch_storage_and_wait(nds_device_storage_request *request, std::int32_t completion_timeout_ms);
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

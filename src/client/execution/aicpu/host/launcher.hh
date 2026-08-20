#ifndef NDS_AICPU_LAUNCHER_HH
#define NDS_AICPU_LAUNCHER_HH

#include "nds/acl_loader.h"
#include "nds/device_operator_args.h"
#include "nds/device_operations.h"
#include "nds/device_storage.h"
#include "nds/result.hh"

#include <cstddef>
#include <cstdint>
#include <string>

namespace nds {

class AicpuEntrypointLauncher {
public:
    AicpuEntrypointLauncher() = default;
    ~AicpuEntrypointLauncher();
    AicpuEntrypointLauncher(const AicpuEntrypointLauncher &) = delete;
    AicpuEntrypointLauncher &operator=(const AicpuEntrypointLauncher &) = delete;

    Result<void> load(NdsAclApi *acl, const std::string &kernel_config_path);
    Result<void> launch_and_wait(NdsDeviceOperationRequest *request, std::int32_t completion_timeout_ms);
    Result<void> launch_post_send_and_wait(NdsDevicePostSendRequest *request, std::int32_t completion_timeout_ms);
    Result<void> launch_storage_read_and_wait(NdsDeviceStorageReadArgs *args, std::int32_t completion_timeout_ms);
    Result<void> launch_storage_write_and_wait(NdsDeviceStorageWriteArgs *args, std::int32_t completion_timeout_ms);
    Result<void> launch_storage_batch_read_and_wait(NdsDeviceStorageBatchReadArgs *args,
                                                    std::int32_t completion_timeout_ms);
    Result<void> launch_storage_batch_write_and_wait(NdsDeviceStorageBatchWriteArgs *args,
                                                     std::int32_t completion_timeout_ms);
    Result<void> launch_storage_wait_and_wait(NdsDeviceStorageWaitArgs *args, std::int32_t completion_timeout_ms);
    void reset() noexcept;
    bool loaded() const noexcept;

private:
    Result<void> launch_storage_and_wait(void *args, std::size_t size, const NdsDeviceStorageContext *context,
                                         std::uint64_t result_address, const char *operator_name,
                                         std::int32_t completion_timeout_ms);
    NdsAclApi *acl_{};
    NdsAclBinHandle binary_{};
    NdsAclFuncHandle function_{};
    NdsAclStream stream_{};
};

}  // namespace nds

#endif

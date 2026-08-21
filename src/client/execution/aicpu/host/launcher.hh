#ifndef NDS_AICPU_LAUNCHER_HH
#define NDS_AICPU_LAUNCHER_HH

#include "nds/acl_loader.h"
#include "nds/device_transport.h"
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
    Result<void> launch_post_send_and_wait(NdsDevicePostSendArgs *args, std::int32_t completion_timeout_ms);
    Result<void> launch_post_recv_and_wait(NdsDevicePostRecvArgs *args, std::int32_t completion_timeout_ms);
    Result<void> launch_poll_cq_and_wait(NdsDevicePollCqArgs *args, std::int32_t completion_timeout_ms);
    Result<void> launch_rdma_send_and_wait(NdsDeviceRdmaSendArgs *args, std::int32_t completion_timeout_ms);
    Result<void> launch_rdma_recv_and_wait(NdsDeviceRdmaRecvArgs *args, std::int32_t completion_timeout_ms);
    Result<void> launch_rdma_read_and_wait(NdsDeviceRdmaReadArgs *args, std::int32_t completion_timeout_ms);
    Result<void> launch_rdma_write_and_wait(NdsDeviceRdmaWriteArgs *args, std::int32_t completion_timeout_ms);
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
    Result<void> launch_operator_and_wait(void *args, std::size_t size, const char *operator_name,
                                          std::int32_t completion_timeout_ms);
    Result<void> launch_storage_and_wait(void *args, std::size_t size, const NdsDeviceStorageContext *context,
                                         const char *operator_name,
                                         std::int32_t completion_timeout_ms);
    NdsAclApi *acl_{};
    NdsAclBinHandle binary_{};
    NdsAclFuncHandle function_{};
    NdsAclStream stream_{};
};

}  // namespace nds

#endif

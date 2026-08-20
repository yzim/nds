#ifndef NDS_AIV_LAUNCHER_HH
#define NDS_AIV_LAUNCHER_HH

#include "nds/acl_loader.h"
#include "nds/device_operations.h"
#include "nds/device_storage.h"
#include "nds/ra_loader.h"
#include "nds/result.hh"

#include <cstdint>
#include <string>

namespace nds {

/* Loads the AIV binary and launches its typed operator entries. */
class AivEntrypointLauncher {
public:
    AivEntrypointLauncher() = default;
    ~AivEntrypointLauncher();
    AivEntrypointLauncher(const AivEntrypointLauncher &) = delete;
    AivEntrypointLauncher &operator=(const AivEntrypointLauncher &) = delete;

    Result<void> load(NdsAclApi *acl, const std::string &kernel_path);
    Result<NdsDeviceOperationRequest> make_device_request(const NdsDeviceOperationRequest &request);
    Result<void> launch_and_wait(std::uint64_t device_request_address, std::uint32_t operation,
                                 std::int32_t completion_timeout_ms);
    Result<void> launch_post_send_and_wait(std::uint64_t device_request_address, std::int32_t completion_timeout_ms);
    Result<void> launch_storage_and_wait(std::uint64_t device_request_address, StorageOperation operation,
                                         std::int32_t completion_timeout_ms);
    void reset() noexcept;
    bool loaded() const noexcept;

private:
    NdsAclApi *acl_{};
    NdsAclBinHandle binary_{};
    NdsAclFuncHandle function_{};
    NdsAclStream stream_{};
};

}  // namespace nds

#endif

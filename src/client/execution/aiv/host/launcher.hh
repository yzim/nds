#ifndef NDS_AIV_LAUNCHER_HH
#define NDS_AIV_LAUNCHER_HH

#include "nds/acl_loader.h"
#include "nds/device_operations.h"
#include "nds/ra_loader.h"

#include <cstdint>
#include <string>

namespace nds {

/* Loads the AIV dataplane binary and launches its connection-operation entry. */
class AivConnectionLauncher {
public:
    AivConnectionLauncher() = default;
    ~AivConnectionLauncher();
    AivConnectionLauncher(const AivConnectionLauncher &) = delete;
    AivConnectionLauncher &operator=(const AivConnectionLauncher &) = delete;

    bool load(nds_acl_api *acl, const std::string &kernel_path);
    bool make_device_request(const nds_device_operation_request &request,
                             nds_device_operation_request *output);
    bool launch_and_wait(std::uint64_t device_request_address, std::uint32_t operation,
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

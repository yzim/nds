#ifndef NDS_AIV_ROCE_HPP
#define NDS_AIV_ROCE_HPP

#include "nds/acl_loader.h"
#include "nds/aiv_roce_abi.h"
#include "nds/ra_loader.h"

#include <cstdint>
#include <string>

namespace nds {

struct AivRdmaWriteRequest {
    nds_ra_ai_data_plane_wq send_wq{};
    std::uint32_t service_level{};
    std::uint32_t local_key{};
    std::uint32_t remote_key{};
    std::uint64_t local_address{};
    std::uint64_t remote_address{};
    std::uint32_t data_size{};
};

/* Loads NDS's AIV binary and launches an AIV entry with a device request pointer. */
class AivRdmaWriteLauncher {
public:
    AivRdmaWriteLauncher() = default;
    ~AivRdmaWriteLauncher();
    AivRdmaWriteLauncher(const AivRdmaWriteLauncher &) = delete;
    AivRdmaWriteLauncher &operator=(const AivRdmaWriteLauncher &) = delete;

    bool load(nds_acl_api &acl, const std::string &kernel_path);
    bool make_device_request(const AivRdmaWriteRequest &request, nds_aiv_rdma_write_request_v1 *output);
    bool launch_noop_and_wait(std::uint64_t device_request_address, std::int32_t completion_timeout_ms);
    bool launch_write_and_wait(std::uint64_t device_request_address, std::int32_t completion_timeout_ms);
    void reset() noexcept;
    bool loaded() const noexcept;
    const std::string &error() const noexcept;

private:
    bool launch_and_wait(const char *function_name, std::uint64_t device_request_address,
                         std::int32_t completion_timeout_ms);
    void set_error(std::string message);

    nds_acl_api *acl_{};
    nds_acl_bin_handle binary_{};
    nds_acl_func_handle noop_function_{};
    nds_acl_func_handle write_function_{};
    nds_acl_stream stream_{};
    std::string error_;
};

} // namespace nds

#endif

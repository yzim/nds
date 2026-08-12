#ifndef NDS_AICPU_ROCE_HPP
#define NDS_AICPU_ROCE_HPP

#include "nds/acl_loader.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace nds {

/*
 * CANN 9.0.0 packaged CCL AICPU transport ABI.  This is deliberately an
 * installation-pinned adapter: NDS loads the vendor-provided JSON/kernel
 * package at runtime and never vendors HCOMM's implementation or payload.
 */
#pragma pack(push, 4)
struct AicpuRoceQpInfo {
    std::uint64_t qp_ptr;
    std::uint32_t sq_index;
    std::uint32_t db_index;
    std::uint16_t retry_count;
    std::uint16_t retry_timeout;
};
#pragma pack(pop)

struct AicpuRoceTxParameters {
    std::uint32_t local_key;
    std::uint32_t remote_key;
    AicpuRoceQpInfo qp;
    std::uint64_t remote_address;
    std::uint64_t local_address;
    std::uint64_t data_size;
    std::uint64_t timeout;
    std::uint64_t local_flag_address;
    std::uint64_t remote_flag_address;
    std::uint32_t local_flag_key;
    std::uint32_t remote_flag_key;
};

static_assert(sizeof(AicpuRoceQpInfo) == 20, "CANN 9.0.0 RunTransportRoceTx QP ABI changed");
static_assert(sizeof(AicpuRoceTxParameters) == 88, "CANN 9.0.0 RunTransportRoceTx ABI changed");

struct AicpuRoceTxRequest {
    std::uint32_t local_key{};
    std::uint32_t remote_key{};
    AicpuRoceQpInfo qp{};
    std::uint64_t remote_address{};
    std::uint64_t local_address{};
    std::uint64_t data_size{};
    std::uint64_t local_flag_address{};
    std::uint64_t remote_flag_address{};
    std::uint32_t local_flag_key{};
    std::uint32_t remote_flag_key{};
    std::uint64_t transport_timeout{1836U};
    std::uint16_t launch_timeout_seconds{1836U};
};

class AicpuRoceTxLauncher {
public:
    AicpuRoceTxLauncher() = default;
    ~AicpuRoceTxLauncher();
    AicpuRoceTxLauncher(const AicpuRoceTxLauncher &) = delete;
    AicpuRoceTxLauncher &operator=(const AicpuRoceTxLauncher &) = delete;

    bool load(nds_acl_api &acl, const std::string &kernel_config_path);
    bool launch_and_wait(const AicpuRoceTxRequest &request, std::int32_t completion_timeout_ms);
    void reset() noexcept;
    bool loaded() const noexcept;
    const std::string &error() const noexcept;

private:
    void set_error(std::string message);

    nds_acl_api *acl_{};
    nds_acl_bin_handle binary_{};
    nds_acl_stream stream_{};
    std::string error_;
};

} // namespace nds

#endif

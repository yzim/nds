#ifndef NDS_CLIENT_BACKENDS_LAUNCH_CONFIG_HH
#define NDS_CLIENT_BACKENDS_LAUNCH_CONFIG_HH

#include <acl/acl_rt.h>

#include <cstdint>

namespace nds::client {

/*
 * The host-side equivalent of a kernel <<< >>> launch configuration.
 *
 * The caller owns `stream` and uses normal ACL event or stream APIs for
 * ordering. A backend launcher neither creates nor destroys this stream.
 */
struct LaunchConfig {
    std::uint32_t block_dim{1U};
    void *l2ctrl{};
    aclrtStream stream{};
    aclrtLaunchKernelCfg *kernel_config{};
    std::uint32_t flags{};
    bool sync{true};
    std::int32_t sync_timeout_ms{5000};
};

}  // namespace nds::client

#endif

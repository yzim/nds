#ifndef NDS_CLIENT_BACKEND_AICPU_LAUNCH_ARGS_HH
#define NDS_CLIENT_BACKEND_AICPU_LAUNCH_ARGS_HH

#include <stdint.h>

/*
 * The AICPU ABI receives these bytes from aclrtLaunchKernelWithHostArgs.
 * `request` contains caller-owned device descriptors and addresses.  The
 * launcher supplies the separate device-visible status location.
 */
template <typename Request>
struct NdsAicpuLaunchArgs {
    Request request;
    uint64_t return_value_address;
};

#endif

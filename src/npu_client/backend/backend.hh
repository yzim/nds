#ifndef NDS_NPU_BACKEND_HH
#define NDS_NPU_BACKEND_HH

#include "nds/npu_ra_qp.hh"

#include <cstdint>
#include <string>

namespace nds {

class NpuRaContext;

struct BackendConfig {
    NpuBackendMode mode{NpuBackendMode::HostRa};
    std::string aicpu_kernel_config;
    std::string aiv_kernel;
};

bool post_send(NpuRaContext *context, NpuRaQp *qp, const BackendConfig &config, std::uint64_t address,
               std::uint32_t length, std::uint32_t local_key, std::string *error);

}  // namespace nds

#endif

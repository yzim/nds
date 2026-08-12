#ifndef NDS_HOST_RA_HPP
#define NDS_HOST_RA_HPP

#include "nds/npu_ra_context.hpp"
#include "nds/npu_ra_qp.hpp"

#include <cstdint>
#include <string>

namespace nds {

struct HostRaWriteRequest {
    nds_ra_sge source{};
    std::uint64_t remote_address{};
    std::uint32_t remote_key{};
};

bool submit_host_ra_write(NpuRaContext &context, NpuRaQp &qp,
                          const HostRaWriteRequest &request, std::string &error);

} // namespace nds

#endif

#ifndef NDS_HOST_RA_HPP
#define NDS_HOST_RA_HPP

#include "nds/npu_ra_context.hh"
#include "nds/npu_ra_qp.hh"

#include <cstdint>
#include <string>

namespace nds {

struct HostRaPostRequest {
    nds_ra_sge source{};
    std::uint32_t opcode{NDS_RA_WR_SEND};
    std::uint64_t remote_address{};
    std::uint32_t remote_key{};
};

bool post_host_ra(NpuRaContext *context, NpuRaQp *qp, const HostRaPostRequest &request, std::string *error);

}  // namespace nds

#endif

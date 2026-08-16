#ifndef NDS_HOST_RA_HPP
#define NDS_HOST_RA_HPP

#include "nds/npu_ra_context.hh"
#include "nds/npu_ra_qp.hh"
#include "nds/result.hh"

#include <cstdint>
#include <string>

namespace nds {

struct HostRaPostRequest {
    nds_ra_sge source{};
    std::uint32_t opcode{NDS_RA_WR_SEND};
    std::uint64_t remote_address{};
    std::uint32_t remote_key{};
};

/* Host RA qp data plane: post a work request. Does not ring the doorbell. */
Result<void> post_host_ra_wr(NpuRaQp *qp, const HostRaPostRequest &request, bool signaled,
                             nds_ra_send_response *response);
Result<std::uint32_t> poll_host_ra_cq(NpuRaQp *qp, nds_ra_completion *completions, std::uint32_t max_entries);

/* Post plus runtime doorbell. */
Result<void> post_host_ra(NpuRaContext *context, NpuRaQp *qp, const HostRaPostRequest &request);

}  // namespace nds

#endif

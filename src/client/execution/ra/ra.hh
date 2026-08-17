#ifndef NDS_RA_HPP
#define NDS_RA_HPP

#include "nds/npu_ra_context.hh"
#include "nds/npu_ra_qp.hh"
#include "nds/protocol.h"
#include "nds/result.hh"

#include <cstdint>
#include <string>

namespace nds {

struct RaPostRequest {
    nds_ra_sge source{};
    std::uint32_t opcode{NDS_RA_WR_SEND};
    std::uint64_t remote_address{};
    std::uint32_t remote_key{};
};

/* RA verbs: post a work request. Does not ring the doorbell. */
Result<void> post_ra_wr(NpuRaQp *qp, const RaPostRequest &request, bool signaled,
                             nds_ra_send_response *response);
Result<std::uint32_t> poll_ra_cq(NpuRaQp *qp, nds_ra_completion *completions, std::uint32_t max_entries);

/* Post plus runtime doorbell. */
Result<void> post_ra(NpuRaContext *context, NpuRaQp *qp, const RaPostRequest &request);

struct RaStorageRequest {
    NpuRaContext *context{};
    NpuRaQp *qp{};
    void *command_device{};
    nds_ra_sge command{};
    void *completion_device{};
    nds_ra_sge completion{};
    nds_protocol_memory remote_data{};
    std::uint16_t operation{};
    std::uint64_t offset{};
    std::uint32_t length{};
    std::uint64_t capacity{};
    std::uint64_t request_id{};
};

/* RA storage layer: encode, post the command, and poll terminal completion. */
Result<void> execute_ra_storage(const RaStorageRequest &request);

}  // namespace nds

#endif

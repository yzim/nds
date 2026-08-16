#include "nds/host_ra.hh"
#include "nds/logging.hh"

#include <string>

namespace nds {

Result<void> post_host_ra_wr(NpuRaQp *qp, const HostRaPostRequest &request, bool signaled,
                             nds_ra_send_response *response) {
    if (qp == nullptr || response == nullptr || qp->execution_mode() != NpuExecutionMode::HostRa ||
        !qp->connected() || qp->ra_api() == nullptr || qp->qp_handle() == nullptr || request.source.address == 0U ||
        request.source.length == 0U || request.source.local_key == 0U ||
        (request.opcode != NDS_RA_WR_SEND && request.opcode != NDS_RA_WR_RDMA_WRITE &&
         request.opcode != NDS_RA_WR_RDMA_READ) ||
        (request.opcode != NDS_RA_WR_SEND && (request.remote_address == 0U || request.remote_key == 0U)) ||
        (request.opcode == NDS_RA_WR_SEND && (request.remote_address != 0U || request.remote_key != 0U))) {
        return unexpected(ErrorCode::kInvalidArgument,
                          "Host RA post requires a connected Host RA QP, valid local SGE, supported opcode, "
                          "and matching remote-memory metadata");
    }
    nds_ra_sge *local = qp->posted_send_sge();
    *local = request.source;
    nds_ra_send_wr wr{};
    wr.buffers = local;
    wr.buffer_count = 1U;
    wr.remote_address = request.remote_address;
    wr.remote_key = request.remote_key;
    wr.opcode = request.opcode;
    wr.send_flags = signaled ? NDS_RA_SEND_SIGNALED : 0;
    *response = {};
    const int result = qp->ra_api()->ra_typical_send_wr(qp->qp_handle(), &wr, response);
    if (result != 0)
        return unexpected(ErrorCode::kRa, "RaTypicalSendWr failed: " + std::to_string(result));
    return {};
}

Result<std::uint32_t> poll_host_ra_cq(NpuRaQp *qp, nds_ra_completion *completions, std::uint32_t max_entries) {
    if (qp == nullptr || completions == nullptr || max_entries == 0U ||
        qp->execution_mode() != NpuExecutionMode::HostRa || !qp->created() || qp->ra_api() == nullptr ||
        qp->qp_handle() == nullptr) {
        return unexpected(ErrorCode::kInvalidArgument,
                          "Host RA CQ poll requires a Host RA QP, output completion storage, and nonzero entry count");
    }
    const int result = qp->ra_api()->ra_poll_cq(qp->qp_handle(), true, max_entries, completions);
    if (result < 0 || static_cast<std::uint32_t>(result) > max_entries) {
        return unexpected(ErrorCode::kRa, "RaPollCq(send) returned an invalid result: " + std::to_string(result) +
                                              " for max_entries=" + std::to_string(max_entries));
    }
    return static_cast<std::uint32_t>(result);
}

Result<void> post_host_ra(NpuRaContext *context, NpuRaQp *qp, const HostRaPostRequest &request) {
    nds_ra_send_response response{};
    if (context == nullptr || qp == nullptr)
        return unexpected(ErrorCode::kInvalidArgument, "Host RA posting requires a context and QP");
    if (const auto posted = post_host_ra_wr(qp, request, true, &response); !posted)
        return unexpected(posted.error());
    NDS_LOG_INFO("npu-client",
                 "Posted one signaled Host RA work request: opcode={} doorbell_index={} doorbell_info=0x{:x}",
                 request.opcode, response.doorbell.db_index, response.doorbell.db_info);
    if (!context->ring_rdma_doorbell(response.doorbell.db_index,
                                     static_cast<std::uint64_t>(response.doorbell.db_info))) {
        return unexpected(ErrorCode::kRuntime, context->error());
    }
    NDS_LOG_INFO("npu-client", "Rang the OPBASE RDMA doorbell on the runtime default stream.");
    return {};
}

}  // namespace nds

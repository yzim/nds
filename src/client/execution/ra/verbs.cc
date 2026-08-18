#include "ra.hh"

#include <string>

namespace nds {

namespace {
int ra_opcode(std::uint32_t opcode) {
    switch (opcode) {
    case NDS_DEVICE_WR_SEND: return NDS_RA_WR_SEND;
    case NDS_DEVICE_WR_RDMA_READ: return NDS_RA_WR_RDMA_READ;
    case NDS_DEVICE_WR_RDMA_WRITE: return NDS_RA_WR_RDMA_WRITE;
    default: return -1;
    }
}
}  // namespace

Result<nds_ra_send_response> NdsRaPostSend(NpuRaQp *qp, const nds_device_send_wr &request) {
    const int opcode = ra_opcode(request.opcode);
    if (qp == nullptr || qp->execution_mode() != NpuExecutionMode::Ra || !qp->connected() ||
        qp->ra_api() == nullptr || qp->qp_handle() == nullptr || request.local.address == 0U ||
        request.local.length == 0U || request.local.local_key == 0U || opcode < 0 ||
        (request.opcode != NDS_DEVICE_WR_SEND && (request.remote_address == 0U || request.remote_key == 0U)) ||
        (request.opcode == NDS_DEVICE_WR_SEND && (request.remote_address != 0U || request.remote_key != 0U))) {
        return unexpected(ErrorCode::kInvalidArgument,
                          "RA post requires a connected RA QP, valid local SGE, supported opcode, "
                          "and matching remote-memory metadata");
    }
    nds_ra_sge *local = qp->posted_send_sge();
    *local = {request.local.address, request.local.length, request.local.local_key};
    nds_ra_send_wr wr{};
    wr.buffers = local;
    wr.buffer_count = 1U;
    wr.remote_address = request.remote_address;
    wr.remote_key = request.remote_key;
    wr.opcode = static_cast<std::uint32_t>(opcode);
    wr.send_flags = (request.flags & NDS_DEVICE_SEND_SIGNALED) != 0U ? NDS_RA_SEND_SIGNALED : 0;
    nds_ra_send_response response{};
    const int result = qp->ra_api()->ra_typical_send_wr(qp->qp_handle(), &wr, &response);
    if (result != 0)
        return unexpected(ErrorCode::kRa, "RaTypicalSendWr failed: " + std::to_string(result));
    return response;
}

Result<void> NdsRaPostRecv(NpuRaQp *qp, const nds_device_recv_wr &request) {
    if (qp == nullptr || qp->execution_mode() != NpuExecutionMode::Ra || !qp->connected() ||
        qp->ra_api() == nullptr || qp->ra_api()->ra_recv_wrlist == nullptr || qp->qp_handle() == nullptr ||
        request.local.address == 0U || request.local.length == 0U || request.local.local_key == 0U) {
        return unexpected(ErrorCode::kInvalidArgument, "RA receive post requires a connected RA QP and valid SGE");
    }
    nds_ra_recv_wr wr{request.wr_id,
                      {request.local.address, request.local.length, request.local.local_key}};
    unsigned int completed{};
    const int result = qp->ra_api()->ra_recv_wrlist(qp->qp_handle(), &wr, 1U, &completed);
    if (result != 0 || completed != 1U)
        return unexpected(ErrorCode::kRa, "RaRecvWrlist failed: " + std::to_string(result));
    return {};
}

Result<std::uint32_t> NdsRaPollCq(NpuRaQp *qp, std::uint32_t queue_kind,
                                  nds_device_completion_output *output) {
    if (qp == nullptr || output == nullptr ||
        (queue_kind != NDS_DEVICE_SEND_QUEUE && queue_kind != NDS_DEVICE_RECEIVE_QUEUE) ||
        qp->execution_mode() != NpuExecutionMode::Ra || !qp->created() || qp->ra_api() == nullptr ||
        qp->qp_handle() == nullptr) {
        return unexpected(ErrorCode::kInvalidArgument, "RA CQ poll requires an RA QP, queue kind, and output");
    }
    nds_ra_completion completions[NDS_DEVICE_MAX_COMPLETIONS]{};
    const int result = qp->ra_api()->ra_poll_cq(qp->qp_handle(), queue_kind == NDS_DEVICE_SEND_QUEUE,
                                                NDS_DEVICE_MAX_COMPLETIONS, completions);
    if (result < 0 || result > static_cast<int>(NDS_DEVICE_MAX_COMPLETIONS)) {
        return unexpected(ErrorCode::kRa, "RaPollCq returned an invalid result: " + std::to_string(result));
    }
    *output = {};
    output->count = static_cast<std::uint32_t>(result);
    for (int index = 0; index < result; ++index) {
        const nds_ra_completion &source = completions[index];
        output->entries[index] = {source.wr_id, source.status, source.opcode, source.vendor_error,
                                  source.byte_length, source.qp_number, source.flags,
                                  source.immediate_data_or_invalidated_rkey, 0U};
    }
    return output->count;
}

}  // namespace nds

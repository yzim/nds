#include "ra.hh"

#include <string>

namespace nds {

namespace {
int ra_opcode(std::uint32_t opcode) {
    switch (opcode) {
        case NDS_DEVICE_WR_SEND:
            return NDS_RA_WR_SEND;
        case NDS_DEVICE_WR_RDMA_READ:
            return NDS_RA_WR_RDMA_READ;
        case NDS_DEVICE_WR_RDMA_WRITE:
            return NDS_RA_WR_RDMA_WRITE;
        default:
            return -1;
    }
}

Result<NdsRaSendResponse> post_unrung(client::QueuePair *qp, const NdsDeviceSendWr &request) {
    const int opcode = ra_opcode(request.opcode);
    if (qp == nullptr || qp->backend_mode() != client::NpuBackend::Ra || !qp->connected() || qp->ra_api() == nullptr ||
        qp->handle() == nullptr || request.local.address == 0U || request.local.length == 0U ||
        request.local.local_key == 0U || opcode < 0 ||
        (request.opcode != NDS_DEVICE_WR_SEND && (request.remote_address == 0U || request.remote_key == 0U)) ||
        (request.opcode == NDS_DEVICE_WR_SEND && (request.remote_address != 0U || request.remote_key != 0U))) {
        return unexpected(ErrorCode::kInvalidArgument,
                          "RA post requires a connected RA QP, valid local SGE, supported opcode, "
                          "and matching remote-memory metadata");
    }
    NdsRaSge *local = qp->posted_send_sge();
    *local = {request.local.address, request.local.length, request.local.local_key};
    NdsRaSendWr wr{};
    wr.buffers = local;
    wr.buffer_count = 1U;
    wr.remote_address = request.remote_address;
    wr.remote_key = request.remote_key;
    wr.opcode = static_cast<std::uint32_t>(opcode);
    wr.send_flags = (request.flags & NDS_DEVICE_SEND_SIGNALED) != 0U ? NDS_RA_SEND_SIGNALED : 0;
    NdsRaSendResponse response{};
    const int result = qp->ra_api()->ra_typical_send_wr(qp->handle(), &wr, &response);
    if (result != 0)
        return unexpected(ErrorCode::kRa, "RaTypicalSendWr failed: " + std::to_string(result));
    return response;
}
}  // namespace

Result<void> NdsRaPostSend(client::Runtime *runtime, client::QueuePair *qp, const NdsDeviceSendWr &request) {
    if (runtime == nullptr)
        return unexpected(ErrorCode::kInvalidArgument, "RA post requires a runtime");
    const auto posted = post_unrung(qp, request);
    if (!posted)
        return unexpected(posted.error());
    auto &api = runtime->cann_runtime_api();
    if (api.set_device == nullptr || api.rdma_db_send == nullptr)
        return unexpected(ErrorCode::kRuntime, "runtime doorbell ABI is unavailable");
    if (const int result = api.set_device(static_cast<std::int32_t>(runtime->config().logical_device_id)); result != 0)
        return unexpected(ErrorCode::kRuntime, "rtSetDevice before rtRDMADBSend failed: " + std::to_string(result));
    if (const int result =
            api.rdma_db_send(posted->doorbell.db_index, static_cast<std::uint64_t>(posted->doorbell.db_info), nullptr);
        result != 0) {
        return unexpected(ErrorCode::kRuntime, "rtRDMADBSend failed: " + std::to_string(result));
    }
    return {};
}

Result<void> NdsRaPostRecv(client::QueuePair *qp, const NdsDeviceRecvWr &request) {
    if (qp == nullptr || qp->backend_mode() != client::NpuBackend::Ra || !qp->connected() || qp->ra_api() == nullptr ||
        qp->ra_api()->ra_recv_wrlist == nullptr || qp->handle() == nullptr || request.local.address == 0U ||
        request.local.length == 0U || request.local.local_key == 0U) {
        return unexpected(ErrorCode::kInvalidArgument, "RA receive post requires a connected RA QP and valid SGE");
    }
    NdsRaRecvWr wr{request.wr_id, {request.local.address, request.local.length, request.local.local_key}};
    unsigned int completed{};
    const int result = qp->ra_api()->ra_recv_wrlist(qp->handle(), &wr, 1U, &completed);
    if (result != 0 || completed != 1U)
        return unexpected(ErrorCode::kRa, "RaRecvWrlist failed: " + std::to_string(result));
    return {};
}

Result<std::uint32_t> NdsRaPollCq(client::QueuePair *qp, bool is_send_cq, std::uint32_t max_completions,
                                  NdsDeviceWc *wc) {
    if (qp == nullptr || wc == nullptr || max_completions == 0U || max_completions > NDS_DEVICE_MAX_COMPLETIONS ||
        qp->backend_mode() != client::NpuBackend::Ra || !qp->created() || qp->ra_api() == nullptr ||
        qp->handle() == nullptr) {
        return unexpected(ErrorCode::kInvalidArgument,
                          "RA completion poll requires an RA QP, output, and supported completion limit");
    }
    NdsRaCompletion completions[NDS_DEVICE_MAX_COMPLETIONS]{};
    const int result = qp->ra_api()->ra_poll_cq(qp->handle(), is_send_cq, max_completions, completions);
    if (result < 0 || result > static_cast<int>(max_completions)) {
        return unexpected(ErrorCode::kRa, "RaPollCq returned an invalid result: " + std::to_string(result));
    }
    for (int index = 0; index < result; ++index) {
        const NdsRaCompletion &source = completions[index];
        wc[index] = {source.wr_id,
                     source.status,
                     source.opcode,
                     source.vendor_error,
                     source.byte_length,
                     source.qp_number,
                     source.flags,
                     source.immediate_data_or_invalidated_rkey,
                     0U};
    }
    return static_cast<std::uint32_t>(result);
}

}  // namespace nds

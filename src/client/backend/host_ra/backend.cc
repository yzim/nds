#include "nds/host_ra.hh"
#include "nds/logging.hh"

namespace nds {

Result<void> post_host_ra(NpuRaContext *context, NpuRaQp *qp, const HostRaPostRequest &request) {
    nds_ra_send_response response{};
    if (context == nullptr || qp == nullptr)
        return failure(ErrorCode::kInvalidArgument, "Host RA posting requires a context and QP");
    if (!qp->post_send(request.source, request.opcode, request.remote_address, request.remote_key, true, &response)) {
        return failure(ErrorCode::kRa, qp->error());
    }
    NDS_LOG_INFO("npu-client",
                 "Posted one signaled Host RA work request: opcode={} doorbell_index={} doorbell_info=0x{:x}",
                 request.opcode, response.doorbell.db_index, response.doorbell.db_info);
    if (!context->ring_rdma_doorbell(response.doorbell.db_index,
                                     static_cast<std::uint64_t>(response.doorbell.db_info))) {
        return failure(ErrorCode::kRuntime, context->error());
    }
    NDS_LOG_INFO("npu-client", "Rang the OPBASE RDMA doorbell on the runtime default stream.");
    return success();
}

}  // namespace nds

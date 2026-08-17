#include "ra.hh"
#include "nds/logging.hh"

#include <string>

namespace nds {

Result<void> post_ra(NpuRaContext *context, NpuRaQp *qp, const RaPostRequest &request) {
    nds_ra_send_response response{};
    if (context == nullptr || qp == nullptr)
        return unexpected(ErrorCode::kInvalidArgument, "RA posting requires a context and QP");
    if (const auto posted = post_ra_wr(qp, request, true, &response); !posted)
        return unexpected(posted.error());
    NDS_LOG_INFO("npu-client",
                 "Posted one signaled RA work request: opcode={} doorbell_index={} doorbell_info=0x{:x}",
                 request.opcode, response.doorbell.db_index, response.doorbell.db_info);
    if (!context->ring_rdma_doorbell(response.doorbell.db_index,
                                     static_cast<std::uint64_t>(response.doorbell.db_info))) {
        return unexpected(ErrorCode::kRuntime, context->error());
    }
    NDS_LOG_INFO("npu-client", "Rang the OPBASE RDMA doorbell on the runtime default stream.");
    return {};
}

}  // namespace nds

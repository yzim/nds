#include "nds/host_ra.hh"
#include "nds/logging.hh"

namespace nds {

bool post_host_ra(NpuRaContext *context, NpuRaQp *qp, const HostRaPostRequest &request, std::string *error) {
    nds_ra_send_response response{};
    if (context == nullptr || qp == nullptr || error == nullptr)
        return false;
    if (!qp->post_send(request.source, request.opcode, request.remote_address, request.remote_key, true, &response)) {
        *error = qp->error();
        return false;
    }
    NDS_LOG_INFO("npu-client",
                 "Posted one signaled Host RA work request: opcode={} doorbell_index={} doorbell_info=0x{:x}",
                 request.opcode, response.doorbell.db_index, response.doorbell.db_info);
    if (!context->ring_rdma_doorbell(response.doorbell.db_index,
                                     static_cast<std::uint64_t>(response.doorbell.db_info))) {
        *error = context->error();
        return false;
    }
    NDS_LOG_INFO("npu-client", "Rang the OPBASE RDMA doorbell on the runtime default stream.");
    error->clear();
    return true;
}

}  // namespace nds

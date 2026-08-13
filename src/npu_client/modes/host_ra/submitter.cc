#include "nds/host_ra.hh"
#include "nds/logging.hh"

#include <iomanip>

namespace nds {

bool submit_host_ra(NpuRaContext &context, NpuRaQp &qp,
                    const HostRaPostRequest &request, std::string &error)
{
    nds_ra_send_response response{};
    if (!qp.post_send(request.source, request.opcode, request.remote_address, request.remote_key, true, response)) {
        error = qp.error();
        return false;
    }
    NDS_LOG_INFO_LINE("npu-client") << "Posted one signaled Host RA work request: opcode=" << request.opcode
              << " doorbell_index=" << response.doorbell.db_index
              << " doorbell_info=0x" << std::hex << response.doorbell.db_info << std::dec << '\n';
    if (!context.submit_rdma_doorbell(response.doorbell.db_index,
                                      static_cast<std::uint64_t>(response.doorbell.db_info))) {
        error = context.error();
        return false;
    }
    NDS_LOG_INFO_LINE("npu-client") << "Submitted OPBASE RDMA doorbell on the runtime default stream.\n";
    error.clear();
    return true;
}

} // namespace nds

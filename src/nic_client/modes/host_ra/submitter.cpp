#include "nds/host_ra.hpp"

#include <iomanip>
#include <iostream>

namespace nds {

bool submit_host_ra_write(NpuRaContext &context, NpuRaQp &qp,
                          const HostRaWriteRequest &request, std::string &error)
{
    nds_ra_send_response response{};
    if (!qp.post_rdma_write(request.source, request.remote_address, request.remote_key, true, response)) {
        error = qp.error();
        return false;
    }
    std::cout << "Posted one signaled RDMA Write: doorbell_index=" << response.doorbell.db_index
              << " doorbell_info=0x" << std::hex << response.doorbell.db_info << std::dec << '\n';
    if (!context.submit_rdma_doorbell(response.doorbell.db_index,
                                      static_cast<std::uint64_t>(response.doorbell.db_info))) {
        error = context.error();
        return false;
    }
    std::cout << "Submitted OPBASE RDMA doorbell on the runtime default stream.\n";
    error.clear();
    return true;
}

} // namespace nds

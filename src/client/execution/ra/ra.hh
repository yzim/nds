#ifndef NDS_RA_HPP
#define NDS_RA_HPP

#include "nds/device_transport.h"
#include "nds/device_storage.h"
#include "nds/npu_ra_context.hh"
#include "nds/npu_ra_qp.hh"
#include "nds/result.hh"

#include <cstdint>

namespace nds {

struct RaConnection {
    NpuRaContext *context{};
    NpuRaQp *qp{};
};

struct RaStorageRequest {
    RaConnection connection{};
    void *command_device{};
    nds_ra_sge command{};
    void *completion_device{};
    nds_ra_sge completion{};
    nds_protocol_memory remote_data{};
    std::uint64_t offset{};
    std::uint32_t length{};
    std::uint64_t capacity{};
    std::uint64_t request_id{};
};

/* Verbs layer. PostSend returns the RA doorbell metadata to its caller. */
Result<nds_ra_send_response> NdsRaPostSend(NpuRaQp *qp, const nds_device_send_wr &wr);
Result<void> NdsRaPostRecv(NpuRaQp *qp, const nds_device_recv_wr &wr);
Result<std::uint32_t> NdsRaPollCq(NpuRaQp *qp, std::uint32_t queue_kind,
                                  nds_device_completion_output *output);

/* Connection layer. Send/Read/Write post and ring the runtime doorbell. */
Result<void> NdsRaRdmaSend(const RaConnection &connection, const nds_device_transfer &transfer);
Result<void> NdsRaRdmaRecv(const RaConnection &connection, const nds_device_transfer &transfer);
Result<void> NdsRaRdmaRead(const RaConnection &connection, const nds_device_transfer &transfer);
Result<void> NdsRaRdmaWrite(const RaConnection &connection, const nds_device_transfer &transfer);

/* Storage layer. Completion is the CPU-written NDS protocol record. */
Result<void> NdsRaStorageRead(const RaStorageRequest &request);
Result<void> NdsRaStorageWrite(const RaStorageRequest &request);

}  // namespace nds

#endif

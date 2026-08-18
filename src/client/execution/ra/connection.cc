#include "ra.hh"
#include "nds/logging.hh"

#include <string>

namespace nds {

namespace {
Result<void> post(NpuRaContext *context, NpuRaQp *qp, const nds_device_send_wr &request) {
    if (context == nullptr || qp == nullptr)
        return unexpected(ErrorCode::kInvalidArgument, "RA posting requires a context and QP");
    const auto posted = NdsRaPostSend(qp, request);
    if (!posted)
        return unexpected(posted.error());
    NDS_LOG_INFO("npu-client",
                 "Posted one signaled RA work request: opcode={} doorbell_index={} doorbell_info=0x{:x}",
                 request.opcode, posted->doorbell.db_index, posted->doorbell.db_info);
    if (!context->ring_rdma_doorbell(posted->doorbell.db_index,
                                     static_cast<std::uint64_t>(posted->doorbell.db_info))) {
        return unexpected(ErrorCode::kRuntime, context->error());
    }
    NDS_LOG_INFO("npu-client", "Rang the OPBASE RDMA doorbell on the runtime default stream.");
    return {};
}
}  // namespace

Result<void> NdsRaRdmaSend(const RaConnection &connection, const nds_device_transfer &transfer) {
    nds_device_send_wr request{};
    nds_device_build_send_wr(&transfer, NDS_DEVICE_WR_SEND, &request);
    return post(connection.context, connection.qp, request);
}

Result<void> NdsRaRdmaRecv(const RaConnection &connection, const nds_device_transfer &transfer) {
    if (connection.context == nullptr || connection.qp == nullptr)
        return unexpected(ErrorCode::kInvalidArgument, "RA receive requires a context and QP");
    nds_device_recv_wr request{};
    nds_device_build_recv_wr(&transfer, &request);
    return NdsRaPostRecv(connection.qp, request);
}

Result<void> NdsRaRdmaRead(const RaConnection &connection, const nds_device_transfer &transfer) {
    nds_device_send_wr request{};
    nds_device_build_send_wr(&transfer, NDS_DEVICE_WR_RDMA_READ, &request);
    return post(connection.context, connection.qp, request);
}

Result<void> NdsRaRdmaWrite(const RaConnection &connection, const nds_device_transfer &transfer) {
    nds_device_send_wr request{};
    nds_device_build_send_wr(&transfer, NDS_DEVICE_WR_RDMA_WRITE, &request);
    return post(connection.context, connection.qp, request);
}

}  // namespace nds

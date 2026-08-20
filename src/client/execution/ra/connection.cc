#include "ra.hh"
namespace nds {

namespace {
Result<void> post(client::Runtime *runtime, client::QueuePair *qp, const NdsDeviceSendWr &request) {
    return NdsRaPostSend(runtime, qp, request);
}
}  // namespace

Result<void> NdsRaRdmaSend(const RaConnection &connection, const NdsDeviceTransfer &transfer) {
    NdsDeviceSendWr request{};
    nds_device_build_send_wr(&transfer, NDS_DEVICE_WR_SEND, &request);
    return post(connection.runtime, connection.qp, request);
}

Result<void> NdsRaRdmaRecv(const RaConnection &connection, const NdsDeviceTransfer &transfer) {
    if (connection.runtime == nullptr || connection.qp == nullptr)
        return unexpected(ErrorCode::kInvalidArgument, "RA receive requires a runtime and QP");
    NdsDeviceRecvWr request{};
    nds_device_build_recv_wr(&transfer, &request);
    return NdsRaPostRecv(connection.qp, request);
}

Result<void> NdsRaRdmaRead(const RaConnection &connection, const NdsDeviceTransfer &transfer) {
    NdsDeviceSendWr request{};
    nds_device_build_send_wr(&transfer, NDS_DEVICE_WR_RDMA_READ, &request);
    return post(connection.runtime, connection.qp, request);
}

Result<void> NdsRaRdmaWrite(const RaConnection &connection, const NdsDeviceTransfer &transfer) {
    NdsDeviceSendWr request{};
    nds_device_build_send_wr(&transfer, NDS_DEVICE_WR_RDMA_WRITE, &request);
    return post(connection.runtime, connection.qp, request);
}

}  // namespace nds

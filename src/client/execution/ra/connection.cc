#include "ra.hh"
namespace nds {

namespace {
Result<void> post(client::Runtime *runtime, client::QueuePair *qp, const NdsDeviceSendWr &request) {
    return NdsRaPostSend(runtime, qp, request);
}
}  // namespace

Result<void> NdsRaRdmaSend(const RaConnection &connection, const NdsDeviceSendWr &request) {
    return post(connection.runtime, connection.qp, request);
}

Result<void> NdsRaRdmaRecv(const RaConnection &connection, const NdsDeviceSendWr &request) {
    if (connection.runtime == nullptr || connection.qp == nullptr)
        return unexpected(ErrorCode::kInvalidArgument, "RA receive requires a runtime and QP");
    NdsDeviceRecvWr recv{};
    recv.wr_id = request.wr_id;
    recv.local = request.local;
    return NdsRaPostRecv(connection.qp, recv);
}

Result<void> NdsRaRdmaRead(const RaConnection &connection, const NdsDeviceSendWr &request) {
    return post(connection.runtime, connection.qp, request);
}

Result<void> NdsRaRdmaWrite(const RaConnection &connection, const NdsDeviceSendWr &request) {
    return post(connection.runtime, connection.qp, request);
}

}  // namespace nds

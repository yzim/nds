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

Result<void> NdsRaRdmaRecv(const RaConnection &connection, const NdsDeviceRecvWr &request) {
    if (connection.runtime == nullptr || connection.qp == nullptr)
        return Error{ErrorCode::kInvalidArgument, "RA receive requires a runtime and QP"};
    return NdsRaPostRecv(connection.qp, request);
}

Result<void> NdsRaRdmaRead(const RaConnection &connection, const NdsDeviceSendWr &request) {
    return post(connection.runtime, connection.qp, request);
}

Result<void> NdsRaRdmaWrite(const RaConnection &connection, const NdsDeviceSendWr &request) {
    return post(connection.runtime, connection.qp, request);
}

}  // namespace nds

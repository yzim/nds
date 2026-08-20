#include "ra.hh"
#include "nds/logging.hh"

#include <string>

namespace nds {

namespace {
Result<void> post(client::Runtime *runtime, client::QueuePair *qp, const NdsDeviceSendWr &request) {
    if (runtime == nullptr || qp == nullptr)
        return unexpected(ErrorCode::kInvalidArgument, "RA posting requires a runtime and QP");
    const auto posted = NdsRaPostSend(qp, request);
    if (!posted)
        return unexpected(posted.error());
    NDS_LOG_INFO("npu-client", "Posted one signaled RA work request: opcode={} doorbell_index={} doorbell_info=0x{:x}",
                 request.opcode, posted->doorbell.db_index, posted->doorbell.db_info);
    auto &api = runtime->runtime_api();
    if (api.set_device == nullptr || api.rdma_db_send == nullptr)
        return unexpected(ErrorCode::kRuntime, "runtime doorbell ABI is unavailable");
    if (const int result = api.set_device(static_cast<std::int32_t>(runtime->config().logical_device_id)); result != 0)
        return unexpected(ErrorCode::kRuntime, "rtSetDevice before rtRDMADBSend failed: " + std::to_string(result));
    if (const int result =
            api.rdma_db_send(posted->doorbell.db_index, static_cast<std::uint64_t>(posted->doorbell.db_info), nullptr);
        result != 0)
        return unexpected(ErrorCode::kRuntime, "rtRDMADBSend failed: " + std::to_string(result));
    NDS_LOG_INFO("npu-client", "Rang the OPBASE RDMA doorbell on the runtime default stream.");
    return {};
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

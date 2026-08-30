#ifndef NDS_CLIENT_BACKEND_RA_LAUNCHER_HH
#define NDS_CLIENT_BACKEND_RA_LAUNCHER_HH

#include "result.hh"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include "device_verbs.h"
#include "ra.hh"

namespace nds {

namespace client {
class Runtime;
class QueuePair;
}  // namespace client

/* Host-side wrapper for the dynamically loaded RA verbs entry points. */
class RaLauncher {
public:
    ~RaLauncher();
    RaLauncher(const RaLauncher &) = delete;
    RaLauncher &operator=(const RaLauncher &) = delete;
    RaLauncher(RaLauncher &&) noexcept = default;
    RaLauncher &operator=(RaLauncher &&) noexcept = default;

    Result<void> load(const std::string &backend_path);
    Result<void> post_send(const NdsDeviceQp &qp, const NdsDeviceSendWr &wr);
    Result<void> post_send(client::Runtime *runtime, client::QueuePair *qp, const NdsDeviceSendWr &wr);
    Result<std::uint32_t> poll_cq(const NdsDeviceQp &qp, std::uint32_t send_cq, std::uint32_t max_completions,
                                  NdsDeviceWc *wc);
    Result<std::uint32_t> poll_cq(client::QueuePair *qp, bool send_cq, std::uint32_t max_completions, NdsDeviceWc *wc);
    Result<void> rdma_send(const NdsDeviceTransport &transport, const NdsDeviceSendWr &wr);
    Result<void> rdma_send(const RaConnection &connection, const NdsDeviceSendWr &wr);
    Result<void> rdma_recv(const NdsDeviceTransport &transport, const NdsDeviceRecvWr &wr);
    Result<void> rdma_recv(const RaConnection &connection, const NdsDeviceRecvWr &wr);
    Result<void> rdma_read(const NdsDeviceTransport &transport, const NdsDeviceSendWr &wr);
    Result<void> rdma_read(const RaConnection &connection, const NdsDeviceSendWr &wr);
    Result<void> rdma_write(const NdsDeviceTransport &transport, const NdsDeviceSendWr &wr);
    Result<void> rdma_write(const RaConnection &connection, const NdsDeviceSendWr &wr);
    Result<void> storage_read(const NdsDeviceStorageContext &context, const StorageReadCommand &command);
    Result<void> storage_read(const RaStorageContext &context, const StorageReadCommand &command);
    Result<void> storage_write(const NdsDeviceStorageContext &context, const StorageWriteCommand &command);
    Result<void> storage_write(const RaStorageContext &context, const StorageWriteCommand &command);
    Result<void> storage_batch_read(const NdsDeviceStorageContext &context, const StorageBatchReadCommand &command);
    Result<void> storage_batch_read(const RaStorageContext &context, const StorageBatchReadCommand &command);
    Result<void> storage_batch_write(const NdsDeviceStorageContext &context, const StorageBatchWriteCommand &command);
    Result<void> storage_batch_write(const RaStorageContext &context, const StorageBatchWriteCommand &command);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace nds

#endif

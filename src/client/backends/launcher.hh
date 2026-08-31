#ifndef NDS_CLIENT_BACKEND_DISPATCHER_HH
#define NDS_CLIENT_BACKEND_DISPATCHER_HH

#include "aicpu/launcher.hh"
#include "aiv/launcher.hh"
#include "backend_mode.hh"
#include "launch_config.hh"
#include "ra/launcher.hh"

#include "endpoint.hh"
#include "result.hh"
#include "device_transport.h"
#include "device_verbs.h"
#include "device_storage.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace nds::client {

class Runtime;

/* Selects one loaded backend. Callers own ACL stream/event lifecycle. */
class BackendLauncher {
public:
    ~BackendLauncher();
    BackendLauncher() = default;
    BackendLauncher(const BackendLauncher &) = delete;
    BackendLauncher &operator=(const BackendLauncher &) = delete;
    BackendLauncher(BackendLauncher &&) noexcept = default;
    BackendLauncher &operator=(BackendLauncher &&) noexcept = default;
    Result<void> open(Runtime *runtime, BackendMode mode, const std::string &artifact);

    Result<NdsDeviceQp> describe_qp(const QueuePair &qp) const;

    Result<void> post_send(const LaunchConfig &launch_config, const NdsDeviceQp &qp, const NdsDeviceSendWr &wr,
                           std::int32_t timeout_ms);
    Result<void> post_recv(const LaunchConfig &launch_config, const NdsDeviceQp &qp, const NdsDeviceRecvWr &wr,
                           std::int32_t timeout_ms);
    Result<void> post_send_batch(Runtime *runtime, const NdsDeviceQp &qp, std::span<const NdsDeviceSendWr> wrs,
                                 std::int32_t timeout_ms);
    Result<std::uint32_t> poll_cq(const LaunchConfig &launch_config, const NdsDeviceQp &qp, bool send_cq,
                                  std::uint32_t max_completions, NdsDeviceWc *completions, std::int32_t timeout_ms);
    Result<void> storage_read(Runtime *runtime, const NdsDeviceStorageContext &context,
                              const StorageReadCommand &command, std::int32_t timeout_ms);
    Result<void> storage_write(Runtime *runtime, const NdsDeviceStorageContext &context,
                               const StorageWriteCommand &command, std::int32_t timeout_ms);
    Result<void> storage_batch_read(Runtime *runtime, const NdsDeviceStorageContext &context,
                                    const StorageBatchReadCommand &command, std::int32_t timeout_ms);
    Result<void> storage_batch_write(Runtime *runtime, const NdsDeviceStorageContext &context,
                                     const StorageBatchWriteCommand &command, std::int32_t timeout_ms);

private:
    BackendMode mode_{BackendMode::Ra};
    Runtime *runtime_{};
    std::unique_ptr<::nds::RaLauncher> ra_;
    std::unique_ptr<::nds::AivLauncher> aiv_;
    std::unique_ptr<::nds::AicpuLauncher> aicpu_;
};

}  // namespace nds::client

#endif

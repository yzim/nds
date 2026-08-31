#ifndef NDS_CLIENT_BACKEND_DISPATCHER_HH
#define NDS_CLIENT_BACKEND_DISPATCHER_HH

#include "aicpu/launcher.hh"
#include "aiv/launcher.hh"
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

/* Selects one backend and owns its loader and private AI launch stream. */
class BackendDispatcher {
public:
    ~BackendDispatcher();
    BackendDispatcher() = default;
    BackendDispatcher(const BackendDispatcher &) = delete;
    BackendDispatcher &operator=(const BackendDispatcher &) = delete;
    BackendDispatcher(BackendDispatcher &&) noexcept = default;
    BackendDispatcher &operator=(BackendDispatcher &&) noexcept = default;
    Result<void> open(NpuBackend mode, const std::string &ra_backend, const std::string &aiv_kernel,
                      const std::string &aicpu_kernel);

    Result<void> post_send(Runtime &runtime, const NdsDeviceQp &qp, const NdsDeviceSendWr &wr, std::int32_t timeout_ms);
    Result<void> post_recv(Runtime &runtime, const NdsDeviceQp &qp, const NdsDeviceRecvWr &wr, std::int32_t timeout_ms);
    Result<void> post_send_batch(Runtime &runtime, const NdsDeviceQp &qp, std::span<const NdsDeviceSendWr> wrs,
                                 std::int32_t timeout_ms);
    Result<std::uint32_t> poll_cq(Runtime &runtime, const NdsDeviceQp &qp, bool send_cq, std::uint32_t max_completions,
                                  NdsDeviceWc *completions, std::int32_t timeout_ms);
    Result<void> storage_read(Runtime &runtime, const NdsDeviceStorageContext &context,
                              const StorageReadCommand &command, std::int32_t timeout_ms);
    Result<void> storage_write(Runtime &runtime, const NdsDeviceStorageContext &context,
                               const StorageWriteCommand &command, std::int32_t timeout_ms);
    Result<void> storage_batch_read(Runtime &runtime, const NdsDeviceStorageContext &context,
                                    const StorageBatchReadCommand &command, std::int32_t timeout_ms);
    Result<void> storage_batch_write(Runtime &runtime, const NdsDeviceStorageContext &context,
                                     const StorageBatchWriteCommand &command, std::int32_t timeout_ms);

private:
    NpuBackend mode_{NpuBackend::Ra};
    std::unique_ptr<::nds::RaLauncher> ra_;
    std::unique_ptr<::nds::AivLauncher> aiv_;
    std::unique_ptr<::nds::AicpuLauncher> aicpu_;
};

}  // namespace nds::client

#endif

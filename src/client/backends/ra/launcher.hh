#ifndef NDS_CLIENT_BACKEND_RA_LAUNCHER_HH
#define NDS_CLIENT_BACKEND_RA_LAUNCHER_HH

#include "backends/launcher.hh"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include "backend_verbs.h"

namespace nds::client {

/* Host-side wrapper for the dynamically loaded RA verbs entry points. */
class RaLauncher final : public Launcher {
public:
    RaLauncher();
    ~RaLauncher();
    RaLauncher(const RaLauncher &) = delete;
    RaLauncher &operator=(const RaLauncher &) = delete;
    RaLauncher(RaLauncher &&) noexcept = default;
    RaLauncher &operator=(RaLauncher &&) noexcept = default;

    static Result<std::unique_ptr<Launcher>> open(const std::string &backend_path);

private:
    Result<void> post_send_with_config(const LaunchConfig &config, const NdsQpDescriptor &qp,
                                       const NdsSendWr &wr) const override;
    Result<void> post_recv_with_config(const LaunchConfig &config, const NdsQpDescriptor &qp,
                                       const NdsRecvWr &wr) const override;
    Result<std::uint32_t> poll_cq_with_config(const LaunchConfig &config, const NdsQpDescriptor &qp, bool send_cq,
                                              std::uint32_t max_completions, NdsWc *wc) const override;
    Result<void> rdma_send_with_config(const LaunchConfig &config, const NdsTransportDescriptor &transport,
                                       std::uint32_t queue_index, const NdsSendWr &wr) const override;
    Result<void> rdma_recv_with_config(const LaunchConfig &config, const NdsTransportDescriptor &transport,
                                       std::uint32_t queue_index, const NdsRecvWr &wr) const override;
    Result<void> rdma_read_with_config(const LaunchConfig &config, const NdsTransportDescriptor &transport,
                                       std::uint32_t queue_index, const NdsSendWr &wr) const override;
    Result<void> rdma_write_with_config(const LaunchConfig &config, const NdsTransportDescriptor &transport,
                                        std::uint32_t queue_index, const NdsSendWr &wr) const override;
    Result<void> storage_bootstrap_with_config(const LaunchConfig &config,
                                               const NdsStorageBootstrapDescriptor &bootstrap) const override;
    Result<void> storage_read_with_config(const LaunchConfig &config, const NdsStorageDescriptor &storage,
                                          std::uint32_t slot_id, std::uint64_t server_offset,
                                          std::uint64_t buffer_address, std::uint32_t buffer_key,
                                          std::uint32_t length) const override;
    Result<void> storage_write_with_config(const LaunchConfig &config, const NdsStorageDescriptor &storage,
                                           std::uint32_t slot_id, std::uint64_t server_offset,
                                           std::uint64_t buffer_address, std::uint32_t buffer_key,
                                           std::uint32_t length) const override;
    Result<void> storage_read_batch_with_config(const LaunchConfig &config, const NdsStorageDescriptor &storage,
                                                std::uint32_t slot_id, std::uint64_t entries_address,
                                                std::uint32_t entries_key, std::uint32_t entry_count) const override;
    Result<void> storage_write_batch_with_config(const LaunchConfig &config, const NdsStorageDescriptor &storage,
                                                 std::uint32_t slot_id, std::uint64_t entries_address,
                                                 std::uint32_t entries_key, std::uint32_t entry_count) const override;
    Result<void> storage_wait_with_config(const LaunchConfig &config, const NdsStorageDescriptor &storage,
                                          std::uint32_t slot_id) const override;

    Result<void> load(const std::string &backend_path);
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace nds::client

#endif

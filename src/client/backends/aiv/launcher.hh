#ifndef NDS_CLIENT_BACKEND_AIV_LAUNCHER_HH
#define NDS_CLIENT_BACKEND_AIV_LAUNCHER_HH

#include "backends/launch_config.hh"
#include "backends/launcher.hh"

#include <acl/acl_rt.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>

namespace nds::client {

class Runtime;

/* Loads and launches NDS AIV device entries through AscendCL. */
class AivLauncher final : public Launcher {
public:
    AivLauncher() = default;
    ~AivLauncher();
    AivLauncher(const AivLauncher &) = delete;
    AivLauncher &operator=(const AivLauncher &) = delete;

    static Result<std::unique_ptr<Launcher>> open(Runtime *runtime, const std::string &kernel_path);
    /* Returns the raw ACL status. The caller owns stream ordering and waits. */
    int launch(const char *kernel_name, const LaunchConfig &config, void *arguments, std::size_t argument_size) const;
    Result<void> launch_and_wait(const char *kernel_name, void *arguments, std::size_t argument_size,
                                 std::uint32_t timeout_ms) const;
    Result<void> load(const std::string &kernel_path);
    void reset() noexcept;
    bool loaded() const noexcept;

private:
    Result<void> post_send_with_config(const LaunchConfig &config, const NdsQpDescriptor &qp,
                                       const NdsSendWr &wr) const override;
    Result<PostSendBatchResult> post_send_batch_with_config(const LaunchConfig &config, const NdsQpDescriptor &qp,
                                                            std::span<const NdsSendWr> wrs) const override;
    Result<void> post_recv_with_config(const LaunchConfig &config, const NdsQpDescriptor &qp,
                                       const NdsRecvWr &wr) const override;
    Result<std::uint32_t> poll_cq_with_config(const LaunchConfig &config, const NdsQpDescriptor &qp, bool send_cq,
                                              std::uint32_t max_completions, NdsWc *completions) const override;
    Result<void> rdma_send_with_config(const LaunchConfig &config, const NdsTransportDescriptor &transport,
                                       std::uint32_t queue_index, const NdsSendWr &wr) const override;
    Result<void> rdma_recv_with_config(const LaunchConfig &config, const NdsTransportDescriptor &transport,
                                       std::uint32_t queue_index, const NdsRecvWr &wr) const override;
    Result<void> rdma_read_with_config(const LaunchConfig &config, const NdsTransportDescriptor &transport,
                                       std::uint32_t queue_index, const NdsSendWr &wr) const override;
    Result<void> rdma_write_with_config(const LaunchConfig &config, const NdsTransportDescriptor &transport,
                                        std::uint32_t queue_index, const NdsSendWr &wr) const override;
    Result<PostSendBatchResult> rdma_send_batch_with_config(const LaunchConfig &config,
                                                            const NdsTransportDescriptor &transport,
                                                            std::uint32_t queue_index,
                                                            std::span<const NdsSendWr> wrs) const override;
    Runtime *runtime_{};
    aclrtBinHandle binary_{};
    mutable std::unordered_map<std::string, aclrtFuncHandle> functions_;
};

}  // namespace nds::client

#endif

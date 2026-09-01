#ifndef NDS_CLIENT_BACKEND_AICPU_LAUNCHER_HH
#define NDS_CLIENT_BACKEND_AICPU_LAUNCHER_HH

#include "backends/launch_config.hh"
#include "backends/launcher.hh"

#include <acl/acl_rt.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace nds::client {

class Runtime;

/* Loads and launches the installed NDS AICPU package through AscendCL. */
class AicpuLauncher final : public Launcher {
public:
    AicpuLauncher() = default;
    ~AicpuLauncher();
    AicpuLauncher(const AicpuLauncher &) = delete;
    AicpuLauncher &operator=(const AicpuLauncher &) = delete;

    static Result<std::unique_ptr<Launcher>> open(Runtime *runtime, const std::string &kernel_path);
    /*
     * AscendCL copies these host argument bytes during submission.  The
     * request therefore needs no intermediate allocation in global memory.
     */
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
    Runtime *runtime_{};
    aclrtBinHandle binary_{};
    mutable std::unordered_map<std::string, aclrtFuncHandle> functions_;
};

}  // namespace nds::client

#endif

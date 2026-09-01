#ifndef NDS_CLIENT_BACKENDS_LAUNCHER_HH
#define NDS_CLIENT_BACKENDS_LAUNCHER_HH

#include "backend_mode.hh"
#include "launch_config.hh"

#include "result.hh"
#include "backend_transport.h"
#include "backend_verbs.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace nds::client {

class Runtime;
class Launcher;

struct PostSendBatchResult {
    // `posted` is the prefix with a device-visible WQE. The transport retires
    // its signaled tail before reporting a partial-post error.
    std::size_t posted{};
    std::int32_t status{};
};

/* Non-owning temporary launch view, analogous to <<< >>>. */
class ConfiguredLauncherView {
public:
    Result<void> post_send(const NdsQpDescriptor &qp, const NdsSendWr &wr) const;
    Result<PostSendBatchResult> post_send_batch(const NdsQpDescriptor &qp, std::span<const NdsSendWr> wrs) const;
    Result<void> post_recv(const NdsQpDescriptor &qp, const NdsRecvWr &wr) const;
    Result<std::uint32_t> poll_cq(const NdsQpDescriptor &qp, bool send_cq, std::uint32_t max_completions,
                                  NdsWc *completions) const;

private:
    friend class Launcher;
    ConfiguredLauncherView(const Launcher *launcher, const LaunchConfig &config);

    const Launcher *launcher_{};
    LaunchConfig config_{};
};

/* Common host interface for exactly one loaded RA, AIV, or AICPU backend. */
class Launcher {
public:
    virtual ~Launcher() = default;
    Launcher(const Launcher &) = delete;
    Launcher &operator=(const Launcher &) = delete;

    static Result<std::unique_ptr<Launcher>> open(Runtime *runtime, BackendMode mode, const std::string &artifact);

    Result<void> post_send(const NdsQpDescriptor &qp, const NdsSendWr &wr) const;
    Result<PostSendBatchResult> post_send_batch(const NdsQpDescriptor &qp, std::span<const NdsSendWr> wrs) const;
    Result<void> post_recv(const NdsQpDescriptor &qp, const NdsRecvWr &wr) const;
    Result<std::uint32_t> poll_cq(const NdsQpDescriptor &qp, bool send_cq, std::uint32_t max_completions,
                                  NdsWc *completions) const;
    Result<void> rdma_send(const LaunchConfig &config, const NdsTransportDescriptor &transport,
                           std::uint32_t queue_index, const NdsSendWr &wr) const;
    Result<void> rdma_recv(const LaunchConfig &config, const NdsTransportDescriptor &transport,
                           std::uint32_t queue_index, const NdsRecvWr &wr) const;
    Result<void> rdma_read(const LaunchConfig &config, const NdsTransportDescriptor &transport,
                           std::uint32_t queue_index, const NdsSendWr &wr) const;
    Result<void> rdma_write(const LaunchConfig &config, const NdsTransportDescriptor &transport,
                            std::uint32_t queue_index, const NdsSendWr &wr) const;
    Result<PostSendBatchResult> rdma_send_batch(const LaunchConfig &config, const NdsTransportDescriptor &transport,
                                                std::uint32_t queue_index, std::span<const NdsSendWr> wrs) const;
    ConfiguredLauncherView with_config(const LaunchConfig &config) const;

protected:
    Launcher() = default;

    virtual Result<void> post_send_with_config(const LaunchConfig &config, const NdsQpDescriptor &qp,
                                               const NdsSendWr &wr) const = 0;
    virtual Result<PostSendBatchResult> post_send_batch_with_config(const LaunchConfig &config,
                                                                    const NdsQpDescriptor &qp,
                                                                    std::span<const NdsSendWr> wrs) const;
    virtual Result<void> post_recv_with_config(const LaunchConfig &config, const NdsQpDescriptor &qp,
                                               const NdsRecvWr &wr) const = 0;
    virtual Result<std::uint32_t> poll_cq_with_config(const LaunchConfig &config, const NdsQpDescriptor &qp,
                                                      bool send_cq, std::uint32_t max_completions,
                                                      NdsWc *completions) const = 0;
    virtual Result<void> rdma_send_with_config(const LaunchConfig &config, const NdsTransportDescriptor &transport,
                                               std::uint32_t queue_index, const NdsSendWr &wr) const = 0;
    virtual Result<void> rdma_recv_with_config(const LaunchConfig &config, const NdsTransportDescriptor &transport,
                                               std::uint32_t queue_index, const NdsRecvWr &wr) const = 0;
    virtual Result<void> rdma_read_with_config(const LaunchConfig &config, const NdsTransportDescriptor &transport,
                                               std::uint32_t queue_index, const NdsSendWr &wr) const = 0;
    virtual Result<void> rdma_write_with_config(const LaunchConfig &config, const NdsTransportDescriptor &transport,
                                                std::uint32_t queue_index, const NdsSendWr &wr) const = 0;
    virtual Result<PostSendBatchResult> rdma_send_batch_with_config(const LaunchConfig &config,
                                                                    const NdsTransportDescriptor &transport,
                                                                    std::uint32_t queue_index,
                                                                    std::span<const NdsSendWr> wrs) const;

private:
    friend class ConfiguredLauncherView;
};

}  // namespace nds::client

#endif

#ifndef NDS_CLIENT_TRANSPORT_HH
#define NDS_CLIENT_TRANSPORT_HH

#include "endpoint.hh"
#include "backends/backend_mode.hh"
#include "runtime.hh"

#include "tcp_socket.hh"
#include "result.hh"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nds {
namespace client {
class Launcher;
}
}  // namespace nds

namespace nds::client {

class Transport;

struct TransportConfig {
    EndpointConfig endpoint;
    QueuePairConfig qp;
    std::uint32_t qp_count{1U};
    std::string server_address;
    std::uint32_t tcp_timeout_ms{5000U};
};

struct BackendConfig {
    BackendMode mode{BackendMode::Ra};
    // The selected backend interprets this as its shared artifact, AIV object,
    // or installed AICPU package descriptor.
    std::string artifact_path;
};

/* Owns one endpoint and a connected indexed set of QPs. Data-path submission
 * is performed by Launcher with the exported device descriptor. */
class Transport {
public:
    Transport();
    ~Transport();

    Result<void> open(Runtime *runtime, const TransportConfig &config, const BackendConfig &backend);

    TcpConnection *exchange_channel() noexcept;
    const nds::QpInfo &local_qp_info() const noexcept;
    const std::vector<nds::QpInfo> &local_qp_infos() const noexcept;
    Result<void> ready();

    Runtime *runtime() noexcept;
    Result<MemoryRegion> register_memory(const MemoryBuffer &buffer, MemoryAccess access);

    // ABI view for backend-owned launch paths such as storage.
    const NdsTransportDescriptor &device_transport() const noexcept;
    Result<NdsQpDescriptor> host_qp_descriptor(std::size_t index) const;

    std::size_t qp_count() const noexcept;
    const BackendConfig &backend() const noexcept;

private:
    friend class StorageClient;

    Result<void> build_device_transport();
    QueuePair *qp() noexcept;
    QueuePair *qp(std::size_t index) noexcept;

    Runtime *runtime_{};
    TransportConfig config_{};
    BackendConfig backend_{};
    Endpoint endpoint_;
    std::vector<QueuePair> qps_;
    // Transport owns the device ABI projection of all of its host QPs.
    // QueuePair itself never exposes or constructs a transport descriptor.
    std::vector<NdsQpDescriptor> host_qp_descriptors_;
    MemoryBuffer device_qp_addresses_;
    MemoryBuffer qp_states_;
    NdsTransportDescriptor device_transport_{};
    TcpConnection exchange_channel_;
    std::vector<nds::QpInfo> local_qps_;
};

}  // namespace nds::client

#endif

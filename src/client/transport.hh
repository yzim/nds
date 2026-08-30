#ifndef NDS_CLIENT_TRANSPORT_HH
#define NDS_CLIENT_TRANSPORT_HH

#include "endpoint.hh"
#include "runtime.hh"

#include "tcp_socket.hh"
#include "result.hh"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace nds {
namespace client {
class BackendDispatcher;
}
}  // namespace nds

namespace nds::client {

class Transport;

struct TransportConfig {
    EndpointConfig endpoint;
    QueuePairConfig qp;
    std::uint32_t qp_count{1U};
    std::string server_address;
    std::uint32_t tcp_timeout_ms{};
};

struct BackendConfig {
    NpuBackend mode{NpuBackend::Ra};
    std::string aicpu_kernel;
    std::string aiv_kernel;
};

/* An opaque reference to one connected transport queue. */
class QueueHandle {
public:
    QueueHandle() = default;

    bool valid() const noexcept;

private:
    friend class Transport;
    QueueHandle(const Transport *owner, std::size_t index) : owner_(owner), index_(index) {}

    const Transport *owner_{};
    std::size_t index_{static_cast<std::size_t>(-1)};
};

/* NDS-owned peer-memory description. It is intentionally independent of any
 * storage protocol record and of provider memory-registration objects. */
struct RemoteMemory {
    std::uint64_t address{};
    std::uint32_t key{};
    std::uint64_t length{};
};

struct TransportSend {
    const MemoryRegion *local{};
    std::uint32_t length{};
    std::uint64_t local_offset{};
};

struct TransportReceive {
    const MemoryRegion *local{};
    std::uint32_t length{};
    std::uint64_t local_offset{};
};

struct TransportRead {
    const MemoryRegion *local{};
    RemoteMemory remote;
    std::uint32_t length{};
    std::uint64_t local_offset{};
};

struct TransportWrite {
    const MemoryRegion *local{};
    RemoteMemory remote;
    std::uint32_t length{};
    std::uint64_t local_offset{};
};

/* Owns one endpoint and a connected indexed set of QPs. Upper layers submit
 * NDS requests through opaque queue handles; they do not access device WRs. */
class Transport {
public:
    Transport();
    ~Transport();

    Result<void> open(Runtime *runtime, const TransportConfig &config, const BackendConfig &backend);

    TcpConnection *exchange_channel() noexcept;
    const nds::transport::QpInfo &local_qp_info() const noexcept;
    const std::vector<nds::transport::QpInfo> &local_qp_infos() const noexcept;
    Result<void> ready();

    Runtime *runtime() noexcept;
    Result<MemoryRegion> register_memory(const MemoryBuffer &buffer, MemoryAccess access);
    Result<QueueHandle> queue(std::size_t index) const;
    Result<void> send(QueueHandle queue, const TransportSend &request);
    Result<void> receive(QueueHandle queue, const TransportReceive &request);
    Result<void> read(QueueHandle queue, const TransportRead &request);
    Result<void> write(QueueHandle queue, const TransportWrite &request);
    Result<void> wait_receive(QueueHandle queue);

    /* Send, Read, and Write batches post one window, signal its final WR, and
     * consume that CQE before returning. Receive batching remains unavailable. */
    Result<void> send_batch(QueueHandle queue, std::span<const TransportSend> requests);
    Result<void> receive_batch(QueueHandle queue, std::span<const TransportReceive> requests);
    Result<void> read_batch(QueueHandle queue, std::span<const TransportRead> requests);
    Result<void> write_batch(QueueHandle queue, std::span<const TransportWrite> requests);

    std::size_t qp_count() const noexcept;
    const BackendConfig &backend() const noexcept;

private:
    friend class StorageClient;

    struct SendRequest {
        const MemoryRegion *local{};
        std::uint32_t length{};
        std::uint64_t local_offset{};
        const RemoteMemory *remote{};
    };

    Result<void> initialize_private_memory();
    Result<void> initialize_launcher();
    Result<void> submit_sends(QueueHandle queue, std::span<const SendRequest> requests, std::uint32_t opcode);
    Result<void> submit_receive(QueueHandle queue, const TransportReceive &request);
    Result<void> complete(QueuePair *qp, bool send_cq);
    QueuePair *queue_pair(QueueHandle queue) noexcept;
    QueuePair *qp() noexcept;
    QueuePair *qp(std::size_t index) noexcept;

    Runtime *runtime_{};
    TransportConfig config_{};
    BackendConfig backend_{};
    Endpoint endpoint_;
    std::vector<QueuePair> qps_;
    std::vector<MemoryBuffer> send_wr_ids_;
    std::vector<MemoryBuffer> receive_wr_ids_;
    std::vector<std::uint64_t> next_wr_ids_;
    std::unique_ptr<BackendDispatcher> backend_dispatcher_;
    TcpConnection exchange_channel_;
    std::vector<nds::transport::QpInfo> local_qps_;
};

}  // namespace nds::client

#endif

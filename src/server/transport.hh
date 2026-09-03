#ifndef NDS_SERVER_TRANSPORT_HH
#define NDS_SERVER_TRANSPORT_HH

#include "endpoint.hh"
#include "result.hh"
#include "tcp_socket.hh"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nds::server {

enum class MemoryAccess {
    LocalRead,
    LocalWrite,
    RemoteRead,
    RemoteWrite,
};

struct TransportConfig {
    EndpointConfig endpoint;
    std::uint32_t max_qp_count{nds::wire::kMaxQpInfoBatch};
    std::uint32_t completion_timeout_ms{5000U};
    std::string listen_address{"0.0.0.0:18515"};
};

class Transport;

/* Owns the TCP listener and creates one isolated transport session per client. */
class TransportListener {
public:
    TransportListener() = default;
    ~TransportListener() = default;
    TransportListener(const TransportListener &) = delete;
    TransportListener &operator=(const TransportListener &) = delete;

    Result<void> open(const TransportConfig &config);
    Result<void> accept(Transport *transport);

private:
    TcpListener tcp_listener_;
    TransportConfig config_{};
};

/* Owns negotiated indexed QPs and the transport-level TCP bootstrap. */
class Transport {
public:
    Transport() = default;
    ~Transport() = default;
    Transport(const Transport &) = delete;
    Transport &operator=(const Transport &) = delete;

    Result<void> post_receive(const MemoryRegion &region);
    Result<void> post_receive(std::size_t qp_index, const MemoryRegion &region);
    Result<void> wait_receive(std::uint32_t timeout_ms);
    Result<void> wait_receive(std::size_t qp_index, std::uint32_t timeout_ms);
    Result<void> send(const MemoryRegion &local, std::uint32_t length);
    Result<void> send(std::size_t qp_index, const MemoryRegion &local, std::uint32_t length);
    Result<std::uint64_t> post_send(const MemoryRegion &local, std::uint32_t length);
    Result<std::uint64_t> post_send(std::size_t qp_index, const MemoryRegion &local, std::uint32_t length);
    Result<void> wait_send(std::uint64_t wr_id, std::uint32_t timeout_ms);
    Result<void> wait_send(std::size_t qp_index, std::uint64_t wr_id, std::uint32_t timeout_ms);
    Result<MemoryRegion> register_memory(void *buffer, std::size_t length, MemoryAccess access);
    Result<void> read(const MemoryRegion &local, std::uint64_t remote_address, std::uint32_t remote_key,
                      std::uint32_t length);
    Result<void> read(std::size_t qp_index, const MemoryRegion &local, std::uint64_t remote_address,
                      std::uint32_t remote_key, std::uint32_t length);
    Result<void> write(const MemoryRegion &local, std::uint64_t remote_address, std::uint32_t remote_key,
                       std::uint32_t length);
    Result<void> write(std::size_t qp_index, const MemoryRegion &local, std::uint64_t remote_address,
                       std::uint32_t remote_key, std::uint32_t length);
    Result<std::uint64_t> post_read(const MemoryRegion &local, std::uint64_t remote_address, std::uint32_t remote_key,
                                    std::uint32_t length);
    Result<std::uint64_t> post_read(std::size_t qp_index, const MemoryRegion &local, std::uint64_t remote_address,
                                    std::uint32_t remote_key, std::uint32_t length);
    Result<void> wait_read(std::uint64_t wr_id, std::uint32_t timeout_ms);
    Result<void> wait_read(std::size_t qp_index, std::uint64_t wr_id, std::uint32_t timeout_ms);
    Result<std::uint64_t> post_write(const MemoryRegion &local, std::uint64_t remote_address, std::uint32_t remote_key,
                                     std::uint32_t length);
    Result<std::uint64_t> post_write(std::size_t qp_index, const MemoryRegion &local, std::uint64_t remote_address,
                                     std::uint32_t remote_key, std::uint32_t length);
    Result<void> wait_write(std::uint64_t wr_id, std::uint32_t timeout_ms);
    Result<void> wait_write(std::size_t qp_index, std::uint64_t wr_id, std::uint32_t timeout_ms);
    Result<std::uint64_t> post_read_batch(std::size_t qp_index, std::span<const TransferRequest> requests);
    Result<void> wait_read_batch(std::size_t qp_index, std::uint64_t wr_id, std::uint32_t timeout_ms);
    Result<std::uint64_t> post_write_batch(std::size_t qp_index, std::span<const TransferRequest> requests);
    Result<void> wait_write_batch(std::size_t qp_index, std::uint64_t wr_id, std::uint32_t timeout_ms);
    Result<void> read_batch(std::size_t qp_index, std::span<const TransferRequest> requests);
    Result<void> write_batch(std::size_t qp_index, std::span<const TransferRequest> requests);
    std::size_t qp_count() const noexcept;
    bool opened() const noexcept;
    TcpConnection *exchange_channel() noexcept;

private:
    friend class TransportListener;

    Result<void> open(TcpConnection exchange_channel, const TransportConfig &config, std::uint32_t qp_count);
    QueuePair *queue_pair(std::size_t qp_index) noexcept;

    Endpoint endpoint_;
    std::vector<QueuePair> qps_;
    std::uint32_t completion_timeout_ms_{5000U};
    TcpConnection exchange_channel_;
    std::vector<nds::QpInfo> local_qps_;
};

}  // namespace nds::server

#endif

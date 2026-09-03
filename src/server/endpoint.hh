#ifndef NDS_SERVER_ENDPOINT_HH
#define NDS_SERVER_ENDPOINT_HH

#include "result.hh"
#include "transport_protocol.hh"

#include <infiniband/verbs.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <span>
#include <string>
#include <vector>

namespace nds::server {

struct EndpointConfig {
    std::string device_name;
    std::uint8_t port{1U};
    std::uint32_t gid_index{};
};

class Endpoint;
class QueuePair;
class Transport;

class MemoryRegion {
public:
    MemoryRegion() = default;
    ~MemoryRegion();
    MemoryRegion(const MemoryRegion &) = delete;
    MemoryRegion &operator=(const MemoryRegion &) = delete;
    MemoryRegion(MemoryRegion &&other) noexcept;
    MemoryRegion &operator=(MemoryRegion &&other) noexcept;

    void *address() const noexcept;
    std::size_t length() const noexcept;
    std::uint32_t local_key() const noexcept;
    std::uint32_t remote_key() const noexcept;
    bool belongs_to(const Endpoint *endpoint) const noexcept;

private:
    friend class Endpoint;
    friend class Transport;
    ibv_mr *mr_{};
    const Endpoint *owner_{};
};

struct TransferRequest {
    const MemoryRegion *local{};
    std::uint64_t local_offset{};
    std::uint64_t remote_address{};
    std::uint32_t remote_key{};
    std::uint32_t length{};
};

class QueuePair {
public:
    QueuePair() = default;
    ~QueuePair();
    QueuePair(const QueuePair &) = delete;
    QueuePair &operator=(const QueuePair &) = delete;
    QueuePair(QueuePair &&other) noexcept;
    QueuePair &operator=(QueuePair &&other) noexcept;

    const nds::QpInfo &local_qp_info() const noexcept;
    Result<void> connect(const nds::QpInfo &peer);
    Result<void> post_receive(const MemoryRegion &region);
    Result<void> wait_receive(std::uint32_t timeout_ms);
    Result<void> send(const MemoryRegion &local, std::uint32_t length, std::uint32_t timeout_ms);
    Result<void> read(const MemoryRegion &local, std::uint64_t remote_address, std::uint32_t remote_key,
                      std::uint32_t length, std::uint32_t timeout_ms);
    Result<void> write(const MemoryRegion &local, std::uint64_t remote_address, std::uint32_t remote_key,
                       std::uint32_t length, std::uint32_t timeout_ms);
    bool created() const noexcept;
    bool connected() const noexcept;

private:
    friend class Endpoint;
    friend class Transport;
    QueuePair(Endpoint *endpoint, const nds::QpInfo &local, ibv_cq *cq, ibv_qp *handle);
    void reset() noexcept;
    bool valid_local_region(const MemoryRegion &region, std::uint64_t local_offset,
                            std::uint32_t length) const noexcept;
    Result<std::uint64_t> post_send(const MemoryRegion &local, std::uint32_t length);
    Result<std::uint64_t> post_transfer(ibv_wr_opcode opcode, const MemoryRegion &local, std::uint64_t local_offset,
                                        std::uint64_t remote_address, std::uint32_t remote_key, std::uint32_t length);
    Result<std::vector<std::uint64_t>> post_transfer_batch(ibv_wr_opcode opcode,
                                                           std::span<const TransferRequest> requests);
    Result<ibv_wc> poll_matching(ibv_wc_opcode opcode, std::uint64_t expected_wr_id, std::uint32_t timeout_ms);
    Result<void> poll(ibv_wc_opcode opcode, std::uint64_t expected_wr_id, std::uint32_t timeout_ms);
    Result<void> transfer(ibv_wr_opcode opcode, const MemoryRegion &local, std::uint64_t remote_address,
                          std::uint32_t remote_key, std::uint32_t length, std::uint32_t timeout_ms);

    Endpoint *endpoint_{};
    ibv_cq *cq_{};
    ibv_qp *handle_{};
    nds::QpInfo local_{};
    bool connected_{};
    std::uint64_t next_send_wr_id_{4U};
    std::uint64_t next_receive_wr_id_{1U};
    std::deque<std::uint64_t> pending_receive_ids_;
    std::deque<ibv_wc> pending_completions_;
};

class Endpoint {
public:
    Endpoint() = default;
    ~Endpoint();
    Endpoint(const Endpoint &) = delete;
    Endpoint &operator=(const Endpoint &) = delete;
    Endpoint(Endpoint &&other) noexcept;
    Endpoint &operator=(Endpoint &&other) noexcept;

    Result<void> open(const EndpointConfig &config);
    Result<QueuePair> create_qp();
    Result<MemoryRegion> reg_mr(void *address, std::size_t length, int access);
    bool opened() const noexcept;

private:
    friend class MemoryRegion;
    friend class QueuePair;
    friend class Transport;
    void reset() noexcept;

    ibv_context *context_{};
    ibv_pd *pd_{};
    EndpointConfig config_{};
    ibv_port_attr port_{};
    ibv_gid gid_{};
    std::uint32_t qp_sequence_{};
};

}  // namespace nds::server

#endif

#ifndef NDS_CPU_VERBS_BACKEND_HH
#define NDS_CPU_VERBS_BACKEND_HH

#include "transport_protocol.hh"
#include "result.hh"

#include <infiniband/verbs.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nds::server {

struct BackendConfig {
    std::string device_name;
    std::uint8_t port{1U};
    std::uint32_t gid_index{};
};

class RegisteredRegion {
public:
    RegisteredRegion() = default;
    ~RegisteredRegion();
    RegisteredRegion(const RegisteredRegion &) = delete;
    RegisteredRegion &operator=(const RegisteredRegion &) = delete;
    RegisteredRegion(RegisteredRegion &&other) noexcept;
    RegisteredRegion &operator=(RegisteredRegion &&other) noexcept;

    void *address() const noexcept;
    std::size_t length() const noexcept;
    std::uint32_t remote_key() const noexcept;

private:
    friend class VerbsBackend;
    std::uint32_t local_key() const noexcept;
    ibv_mr *mr_{};
};

class VerbsBackend {
public:
    VerbsBackend() = default;
    ~VerbsBackend();
    VerbsBackend(const VerbsBackend &) = delete;
    VerbsBackend &operator=(const VerbsBackend &) = delete;

    Result<void> open(const BackendConfig &config, std::uint32_t qp_count);
    Result<void> connect(const std::vector<nds::transport::QpInfo> &peers);
    Result<RegisteredRegion> register_memory(void *address, std::size_t length, int access);
    Result<void> post_receive(std::size_t qp_index, const RegisteredRegion &region);
    Result<void> wait_receive(std::size_t qp_index, std::uint32_t timeout_ms);
    Result<void> send(std::size_t qp_index, const RegisteredRegion &local, std::uint32_t length);
    Result<void> read(std::size_t qp_index, const RegisteredRegion &local, std::uint64_t remote_address,
                      std::uint32_t remote_key, std::uint32_t length);
    Result<void> write(std::size_t qp_index, const RegisteredRegion &local, std::uint64_t remote_address,
                       std::uint32_t remote_key, std::uint32_t length);
    const std::vector<nds::transport::QpInfo> &local_qp_infos() const noexcept;
    std::size_t qp_count() const noexcept;

private:
    struct QueuePair {
        ibv_cq *cq{};
        ibv_qp *handle{};
        nds::transport::QpInfo local{};
    };

    Result<void> transfer(std::size_t qp_index, ibv_wr_opcode opcode, const RegisteredRegion &local,
                          std::uint64_t remote_address, std::uint32_t remote_key, std::uint32_t length);
    Result<void> poll(std::size_t qp_index, ibv_wc_opcode opcode, std::uint32_t timeout_ms);

    ibv_context *context_{};
    ibv_pd *pd_{};
    std::vector<QueuePair> qps_;
    std::vector<nds::transport::QpInfo> local_qps_;
    BackendConfig config_{};
};

}  // namespace nds::server

#endif

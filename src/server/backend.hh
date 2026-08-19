#ifndef NDS_CPU_VERBS_BACKEND_HH
#define NDS_CPU_VERBS_BACKEND_HH

#include "nds/connection.h"
#include "nds/result.hh"

#include <infiniband/verbs.h>

#include <cstddef>
#include <cstdint>
#include <string>

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

    Result<void> open(const BackendConfig &config);
    Result<void> connect(const nds_qp_info &peer);
    Result<RegisteredRegion> register_memory(void *address, std::size_t length, int access);
    Result<void> post_receive(const RegisteredRegion &region);
    Result<void> wait_receive(std::uint32_t timeout_ms);
    Result<void> send(const RegisteredRegion &local, std::uint32_t length);
    Result<void> read(const RegisteredRegion &local, std::uint64_t remote_address, std::uint32_t remote_key,
                      std::uint32_t length);
    Result<void> write(const RegisteredRegion &local, std::uint64_t remote_address, std::uint32_t remote_key,
                       std::uint32_t length);
    const nds_qp_info &local_qp_info() const noexcept;

private:
    Result<void> transfer(ibv_wr_opcode opcode, const RegisteredRegion &local, std::uint64_t remote_address,
                          std::uint32_t remote_key, std::uint32_t length);
    Result<void> poll(ibv_wc_opcode opcode, std::uint32_t timeout_ms);

    ibv_context *context_{};
    ibv_pd *pd_{};
    ibv_cq *cq_{};
    ibv_qp *qp_{};
    nds_qp_info local_{};
    BackendConfig config_{};
};

}  // namespace nds::server

#endif

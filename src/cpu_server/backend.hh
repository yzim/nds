#ifndef NDS_CPU_VERBS_BACKEND_HH
#define NDS_CPU_VERBS_BACKEND_HH

#include "nds/transport.h"

#include <infiniband/verbs.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace nds::cpu {

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

    void *address() const noexcept;
    std::size_t length() const noexcept;

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

    bool open(const BackendConfig &config, std::string *error);
    bool connect(const nds_transport_endpoint &peer, std::string *error);
    bool register_memory(void *address, std::size_t length, int access, RegisteredRegion *region, std::string *error);
    bool post_receive(const RegisteredRegion &region, std::string *error);
    bool wait_receive(std::uint32_t timeout_ms, std::string *error);
    bool read(const RegisteredRegion &local, std::uint64_t remote_address, std::uint32_t remote_key,
              std::uint32_t length, std::string *error);
    bool write(const RegisteredRegion &local, std::uint64_t remote_address, std::uint32_t remote_key,
               std::uint32_t length, std::string *error);
    const nds_transport_endpoint &local_endpoint() const noexcept;

private:
    bool transfer(ibv_wr_opcode opcode, const RegisteredRegion &local, std::uint64_t remote_address,
                  std::uint32_t remote_key, std::uint32_t length, std::string *error);
    bool poll(ibv_wc_opcode opcode, std::uint32_t timeout_ms, std::string *error);

    ibv_context *context_{};
    ibv_pd *pd_{};
    ibv_cq *cq_{};
    ibv_qp *qp_{};
    nds_transport_endpoint local_{};
    BackendConfig config_{};
};

}  // namespace nds::cpu

#endif

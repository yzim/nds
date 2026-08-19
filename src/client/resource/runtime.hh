#ifndef NDS_CLIENT_RUNTIME_HH
#define NDS_CLIENT_RUNTIME_HH

#include "memory.hh"
#include "nds/npu_ra_context.hh"
#include "nds/result.hh"

namespace nds::client {

using RuntimeConfig = NpuRaContextConfig;

/* Owns the process-local CANN/RA lifecycle and its memory service. */
class NpuRuntime {
public:
    NpuRuntime() = default;
    ~NpuRuntime() = default;
    NpuRuntime(const NpuRuntime &) = delete;
    NpuRuntime &operator=(const NpuRuntime &) = delete;

    Result<void> open(const RuntimeConfig &config);

    Memory *memory() noexcept;
    NpuRaContext *context() noexcept;
    const RuntimeConfig &config() const noexcept;

private:
    RuntimeConfig config_{};
    NpuRaContext context_;
    Memory memory_;
};

}  // namespace nds::client

#endif

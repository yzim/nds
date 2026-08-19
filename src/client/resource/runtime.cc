#include "runtime.hh"

namespace nds::client {

Result<void> NpuRuntime::open(const RuntimeConfig &config) {
    if (context_.initialized())
        return unexpected(ErrorCode::kInvalidArgument, "NPU runtime is already open");
    config_ = config;
    if (!context_.initialize(config_))
        return unexpected(ErrorCode::kRuntime, context_.error());
    memory_.attach(&context_);
    return {};
}

Memory *NpuRuntime::memory() noexcept {
    return &memory_;
}

NpuRaContext *NpuRuntime::context() noexcept {
    return &context_;
}

const RuntimeConfig &NpuRuntime::config() const noexcept {
    return config_;
}

}  // namespace nds::client

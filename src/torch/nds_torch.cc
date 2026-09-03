#include "runtime.hh"
#include "storage.hh"
#include "transport.hh"
#include "backends/launcher.hh"

#include <torch/extension.h>
#include <torch_npu/csrc/core/npu/NPUFunctions.h>

#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <utility>

namespace nds::torch {
namespace {

template <typename T>
T value_or_throw(Result<T> result) {
    TORCH_CHECK(result.ok(), "NDS: ", result.error().message);
    return std::move(result).value();
}

void check(Result<void> result) {
    TORCH_CHECK(result.ok(), "NDS: ", result.error().message);
}

void check_tensor(const ::torch::Tensor &tensor) {
    TORCH_CHECK(tensor.device().is_cpu(), "NDS wrappers currently accept CPU tensors only");
    TORCH_CHECK(tensor.is_contiguous(), "NDS wrappers require contiguous tensors");
    TORCH_CHECK(tensor.nbytes() > 0U, "NDS wrappers require a nonempty tensor");
    TORCH_CHECK(tensor.nbytes() <= std::numeric_limits<std::uint32_t>::max(), "NDS tensor is too large");
}

client::BackendMode backend_mode(const std::string &backend_name) {
    if (backend_name == "ra")
        return client::BackendMode::Ra;
    if (backend_name == "aiv")
        return client::BackendMode::Aiv;
    if (backend_name == "aicpu")
        return client::BackendMode::Aicpu;
    TORCH_CHECK(false, "unsupported NDS backend: ", backend_name);
}

class Session : public std::enable_shared_from_this<Session> {
public:
    Session(std::string server, std::string backend_name, std::string backend_artifact_path) {
        TORCH_CHECK(parse_tcp_address(server).ok(), "server must be IPv4:port");
        const auto device = c10_npu::current_device();
        TORCH_CHECK(device >= 0, "torch_npu has no active NPU device");

        client::RuntimeConfig runtime_config;
        runtime_config.logical_device_id = static_cast<std::uint32_t>(device);
        runtime_config.adopt_existing_context = true;
        check(runtime_.open(runtime_config));
        client::TransportConfig transport_config;
        transport_config.server_address = std::move(server);

        client::BackendConfig backend;
        backend.mode = backend_mode(backend_name);
        backend.artifact_path = std::move(backend_artifact_path);
        TORCH_CHECK(!backend.artifact_path.empty(), "NDS backend requires an artifact");
        check(transport_.open(&runtime_, transport_config, backend));
        check(storage_.open(&runtime_, &transport_));
        launcher_ = value_or_throw(client::Launcher::open(&runtime_, backend.mode, backend.artifact_path));
        if (backend.mode != client::BackendMode::Ra)
            TORCH_CHECK(aclrtCreateStream(&stream_) == ACL_SUCCESS, "NDS: storage stream creation failed");
        check(launcher_->with_config({.stream = stream_, .sync = true, .sync_timeout_ms = 5000})
                  .storage_bootstrap(storage_.bootstrap_descriptor()));
        check(storage_.complete_bootstrap(5000U));
    }

    ~Session() {
        if (stream_ != nullptr)
            (void)aclrtDestroyStream(stream_);
    }

    void read_(const ::torch::Tensor &output, std::int64_t offset);
    void write(const ::torch::Tensor &input, std::int64_t offset);
    std::int64_t capacity() const;

private:
    client::Runtime runtime_;
    client::Transport transport_;
    client::StorageClient storage_;
    std::unique_ptr<client::Launcher> launcher_;
    aclrtStream stream_{};
};

void Session::read_(const ::torch::Tensor &output, std::int64_t offset) {
    check_tensor(output);
    TORCH_CHECK(offset >= 0, "storage offset must be nonnegative");
    auto buffer = value_or_throw(runtime_.allocate(output.nbytes(), client::MemoryLocation::Device));
    const auto region = value_or_throw(storage_.register_memory(buffer, client::MemoryAccess::LocalWrite |
                                                                            client::MemoryAccess::RemoteWrite |
                                                                            client::MemoryAccess::RemoteRead));
    const auto slot = value_or_throw(storage_.allocate_slot());
    check(launcher_->with_config({.stream = stream_, .sync = true, .sync_timeout_ms = 5000})
              .storage_read(storage_.descriptor(), slot, static_cast<std::uint64_t>(offset), region.address(),
                            region.remote_key(), static_cast<std::uint32_t>(output.nbytes())));
    check(launcher_->with_config({.stream = stream_, .sync = true, .sync_timeout_ms = 5000})
              .storage_wait(storage_.descriptor(), slot));
    check(storage_.release_slot(slot));
    check(runtime_.copy_from(output.data_ptr(), buffer, output.nbytes()));
}

void Session::write(const ::torch::Tensor &input, std::int64_t offset) {
    check_tensor(input);
    TORCH_CHECK(offset >= 0, "storage offset must be nonnegative");
    auto buffer = value_or_throw(runtime_.allocate(input.nbytes(), client::MemoryLocation::Device));
    check(runtime_.copy_to(&buffer, input.data_ptr(), input.nbytes()));
    const auto region = value_or_throw(storage_.register_memory(buffer, client::MemoryAccess::LocalWrite |
                                                                            client::MemoryAccess::RemoteWrite |
                                                                            client::MemoryAccess::RemoteRead));
    const auto slot = value_or_throw(storage_.allocate_slot());
    check(launcher_->with_config({.stream = stream_, .sync = true, .sync_timeout_ms = 5000})
              .storage_write(storage_.descriptor(), slot, static_cast<std::uint64_t>(offset), region.address(),
                             region.remote_key(), static_cast<std::uint32_t>(input.nbytes())));
    check(launcher_->with_config({.stream = stream_, .sync = true, .sync_timeout_ms = 5000})
              .storage_wait(storage_.descriptor(), slot));
    check(storage_.release_slot(slot));
}

std::int64_t Session::capacity() const {
    return static_cast<std::int64_t>(storage_.capacity());
}

}  // namespace
}  // namespace nds::torch

PYBIND11_MODULE(TORCH_EXTENSION_NAME, module) {
    namespace nds_torch = nds::torch;
    pybind11::class_<nds_torch::Session, std::shared_ptr<nds_torch::Session>>(module, "Session")
        .def(pybind11::init<std::string, std::string, std::string>(), pybind11::arg("server"),
             pybind11::arg("backend") = "ra", pybind11::arg("backend_artifact_path") = "")
        .def("read_", &nds_torch::Session::read_)
        .def("write", &nds_torch::Session::write)
        .def_property_readonly("capacity", &nds_torch::Session::capacity);
}

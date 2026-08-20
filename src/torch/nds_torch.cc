#include "runtime.hh"
#include "storage.hh"
#include "transport.hh"

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
    TORCH_CHECK(result, "NDS: ", result.error().message);
    return std::move(*result);
}

void check(Result<void> result) {
    TORCH_CHECK(result, "NDS: ", result.error().message);
}

void check_tensor(const ::torch::Tensor &tensor) {
    TORCH_CHECK(tensor.device().is_cpu(), "NDS wrappers currently accept CPU tensors only");
    TORCH_CHECK(tensor.is_contiguous(), "NDS wrappers require contiguous tensors");
    TORCH_CHECK(tensor.nbytes() > 0U, "NDS wrappers require a nonempty tensor");
    TORCH_CHECK(tensor.nbytes() <= std::numeric_limits<std::uint32_t>::max(), "NDS tensor is too large");
}

client::NpuExecutionMode execution_mode(const std::string &backend) {
    if (backend == "ra")
        return client::NpuExecutionMode::Ra;
    if (backend == "aiv")
        return client::NpuExecutionMode::Aiv;
    if (backend == "aicpu")
        return client::NpuExecutionMode::Aicpu;
    TORCH_CHECK(false, "unsupported NDS backend: ", backend);
}

class Session : public std::enable_shared_from_this<Session> {
public:
    Session(std::string server, std::string backend, std::string aiv_kernel, std::string aicpu_kernel_config) {
        TORCH_CHECK(parse_tcp_address(server), "server must be IPv4:port");
        const auto device = c10_npu::current_device();
        TORCH_CHECK(device >= 0, "torch_npu has no active NPU device");

        client::RuntimeConfig runtime_config;
        runtime_config.logical_device_id = static_cast<std::uint32_t>(device);
        runtime_config.adopt_existing_context = true;
        check(runtime_.open(runtime_config));
        client::TransportConfig transport_config;
        transport_config.endpoint.ra_library = "libra.so";
        transport_config.server_address = std::move(server);

        client::ExecutionConfig execution;
        execution.mode = execution_mode(backend);
        execution.aiv_kernel = std::move(aiv_kernel);
        execution.aicpu_kernel_config = std::move(aicpu_kernel_config);
        if (execution.mode == client::NpuExecutionMode::Aiv)
            TORCH_CHECK(!execution.aiv_kernel.empty(), "AIV requires an NDS kernel binary");
        if (execution.mode == client::NpuExecutionMode::Aicpu)
            TORCH_CHECK(!execution.aicpu_kernel_config.empty(), "AICPU requires an NDS package configuration");
        check(transport_.open(&runtime_, transport_config, execution));
        check(storage_.open(&runtime_, &transport_));
    }

    void read_(const ::torch::Tensor &output, std::int64_t offset);
    void write(const ::torch::Tensor &input, std::int64_t offset);
    std::int64_t capacity() const;

private:
    client::Runtime runtime_;
    client::Transport transport_;
    client::StorageClient storage_;
};

void Session::read_(const ::torch::Tensor &output, std::int64_t offset) {
    check_tensor(output);
    TORCH_CHECK(offset >= 0, "storage offset must be nonnegative");
    auto buffer = value_or_throw(runtime_.allocate(output.nbytes()));
    const auto completion = value_or_throw(
        storage_.read(static_cast<std::uint64_t>(offset), &buffer, static_cast<std::uint32_t>(output.nbytes())));
    check(storage_.wait(completion, 5000U));
    check(runtime_.copy_from(output.data_ptr(), buffer, output.nbytes()));
}

void Session::write(const ::torch::Tensor &input, std::int64_t offset) {
    check_tensor(input);
    TORCH_CHECK(offset >= 0, "storage offset must be nonnegative");
    auto buffer = value_or_throw(runtime_.allocate(input.nbytes()));
    check(runtime_.copy_to(&buffer, input.data_ptr(), input.nbytes()));
    const auto completion = value_or_throw(
        storage_.write(static_cast<std::uint64_t>(offset), &buffer, static_cast<std::uint32_t>(input.nbytes())));
    check(storage_.wait(completion, 5000U));
}

std::int64_t Session::capacity() const {
    return static_cast<std::int64_t>(storage_.capacity());
}

}  // namespace
}  // namespace nds::torch

PYBIND11_MODULE(TORCH_EXTENSION_NAME, module) {
    namespace nds_torch = nds::torch;
    pybind11::class_<nds_torch::Session, std::shared_ptr<nds_torch::Session>>(module, "Session")
        .def(pybind11::init<std::string, std::string, std::string, std::string>(), pybind11::arg("server"),
             pybind11::arg("backend") = "ra", pybind11::arg("aiv_kernel") = "",
             pybind11::arg("aicpu_kernel_config") = "")
        .def("read_", &nds_torch::Session::read_)
        .def("write", &nds_torch::Session::write)
        .def_property_readonly("capacity", &nds_torch::Session::capacity);
}

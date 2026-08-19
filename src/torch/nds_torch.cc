#include "runtime.hh"
#include "storage.hh"
#include "transport.hh"

#include <torch/extension.h>
#include <torch_npu/csrc/core/npu/NPUFunctions.h>

#include <arpa/inet.h>

#include <charconv>
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

struct ServerEndpoint {
    std::string ipv4;
    std::uint16_t port;
};

ServerEndpoint parse_server_endpoint(const std::string &server) {
    const auto separator = server.rfind(':');
    TORCH_CHECK(separator != std::string::npos && separator > 0U && separator + 1U < server.size(),
                "server must be an IPv4 address followed by ':' and a TCP port");

    ServerEndpoint endpoint{server.substr(0U, separator), 0U};
    in_addr address{};
    TORCH_CHECK(inet_pton(AF_INET, endpoint.ipv4.c_str(), &address) == 1, "server IPv4 address is invalid");

    const auto port = server.substr(separator + 1U);
    const auto [last, error] = std::from_chars(port.data(), port.data() + port.size(), endpoint.port);
    TORCH_CHECK(error == std::errc{} && last == port.data() + port.size() && endpoint.port != 0U,
                "server TCP port is invalid");
    return endpoint;
}

class Session : public std::enable_shared_from_this<Session> {
public:
    Session(std::string server, std::string backend, std::string aiv_kernel, std::string aicpu_kernel_config) {
        auto endpoint = parse_server_endpoint(server);
        const auto device = c10_npu::current_device();
        TORCH_CHECK(device >= 0, "torch_npu has no active NPU device");

        client::RuntimeConfig runtime_config;
        runtime_config.logical_device_id = static_cast<std::uint32_t>(device);
        runtime_config.adopt_existing_context = true;
        check(runtime_.open(runtime_config));
        std::int32_t physical_device{};
        TORCH_CHECK(runtime_.acl_api().get_phy_dev_id != nullptr, "CANN physical-device query is unavailable");
        TORCH_CHECK(runtime_.acl_api().get_phy_dev_id(device, &physical_device) == 0 && physical_device >= 0,
                    "CANN cannot map the active logical device to a physical device");

        client::TransportConfig transport_config;
        transport_config.endpoint.ra_library = "libra.so";
        transport_config.endpoint.physical_device_id = static_cast<std::uint32_t>(physical_device);
        transport_config.cpu_ipv4 = std::move(endpoint.ipv4);
        transport_config.tcp_port = endpoint.port;

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
    check(storage_.read(static_cast<std::uint64_t>(offset), &buffer, static_cast<std::uint32_t>(output.nbytes())));
    check(runtime_.copy_from(output.data_ptr(), buffer, output.nbytes()));
}

void Session::write(const ::torch::Tensor &input, std::int64_t offset) {
    check_tensor(input);
    TORCH_CHECK(offset >= 0, "storage offset must be nonnegative");
    auto buffer = value_or_throw(runtime_.allocate(input.nbytes()));
    check(runtime_.copy_to(&buffer, input.data_ptr(), input.nbytes()));
    check(storage_.write(static_cast<std::uint64_t>(offset), &buffer, static_cast<std::uint32_t>(input.nbytes())));
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

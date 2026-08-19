#include "ra/ra.hh"
#include "runtime.hh"
#include "storage.hh"
#include "transport.hh"

#include <torch/extension.h>

#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

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

struct RegisteredTensor {
    client::MemoryBuffer buffer;
    client::MemoryRegion region;
    ::torch::Tensor tensor;
};

class Session;

class Verbs {
public:
    explicit Verbs(std::shared_ptr<Session> session) : session_(std::move(session)) {}

    std::vector<std::int64_t> post_send(const ::torch::Tensor &input);
    void post_receive(const ::torch::Tensor &output);
    std::int64_t poll_send();
    std::int64_t poll_receive();

private:
    std::shared_ptr<Session> session_;
    std::vector<RegisteredTensor> sends_;
    std::vector<RegisteredTensor> receives_;
};

class Transport {
public:
    explicit Transport(std::shared_ptr<Session> session) : session_(std::move(session)) {}

    void send(const ::torch::Tensor &input);

private:
    std::shared_ptr<Session> session_;
    std::vector<RegisteredTensor> sends_;
};

class Storage {
public:
    explicit Storage(std::shared_ptr<Session> session) : session_(std::move(session)) {}

    void read_(const ::torch::Tensor &output, std::int64_t offset);
    void write(const ::torch::Tensor &input, std::int64_t offset);
    std::int64_t capacity() const;

private:
    std::shared_ptr<Session> session_;
};

class Session : public std::enable_shared_from_this<Session> {
public:
    Session(std::string ascendcl_library, std::string runtime_library, std::string ra_library, std::string npu_ip,
            std::int64_t logical_device, std::int64_t physical_device, std::string cpu_ip, std::int64_t tcp_port,
            std::string backend, std::string aiv_kernel, std::string aicpu_kernel_config) {
        TORCH_CHECK(logical_device >= 0 && logical_device <= std::numeric_limits<std::uint32_t>::max(),
                    "invalid logical device");
        TORCH_CHECK(physical_device >= 0 && physical_device <= std::numeric_limits<std::uint32_t>::max(),
                    "invalid physical device");
        TORCH_CHECK(tcp_port > 0 && tcp_port <= std::numeric_limits<std::uint16_t>::max(), "invalid TCP port");

        client::RuntimeConfig runtime_config;
        runtime_config.ascendcl_library = std::move(ascendcl_library);
        runtime_config.runtime_library = std::move(runtime_library);
        runtime_config.logical_device_id = static_cast<std::uint32_t>(logical_device);
        check(runtime_.open(runtime_config));

        client::TransportConfig transport_config;
        transport_config.endpoint.ra_library = std::move(ra_library);
        transport_config.endpoint.local_ipv4 = std::move(npu_ip);
        transport_config.endpoint.physical_device_id = static_cast<std::uint32_t>(physical_device);
        transport_config.cpu_ipv4 = std::move(cpu_ip);
        transport_config.tcp_port = static_cast<std::uint16_t>(tcp_port);

        client::ExecutionConfig execution;
        execution.mode = execution_mode(backend);
        execution.aiv_kernel = std::move(aiv_kernel);
        execution.aicpu_kernel_config = std::move(aicpu_kernel_config);
        if (execution.mode == client::NpuExecutionMode::Aiv)
            TORCH_CHECK(!execution.aiv_kernel.empty(), "AIV requires an NDS kernel binary");
        if (execution.mode == client::NpuExecutionMode::Aicpu)
            TORCH_CHECK(!execution.aicpu_kernel_config.empty(), "AICPU requires an NDS package configuration");
        check(transport_.open(&runtime_, transport_config, execution));
    }

    std::shared_ptr<Verbs> verbs() {
        return std::make_shared<Verbs>(shared_from_this());
    }
    std::shared_ptr<Transport> transport() {
        return std::make_shared<Transport>(shared_from_this());
    }
    std::shared_ptr<Storage> storage() {
        TORCH_CHECK(!storage_open_, "an NDS session supports one storage command");
        check(storage_.open(&runtime_, &transport_));
        storage_open_ = true;
        return std::make_shared<Storage>(shared_from_this());
    }

    void require_ra(const char *layer) const {
        TORCH_CHECK(transport_.execution().mode == client::NpuExecutionMode::Ra, layer,
                    " wrappers currently require the RA backend");
    }
    RegisteredTensor register_tensor(const ::torch::Tensor &tensor) {
        check_tensor(tensor);
        auto buffer = value_or_throw(runtime_.allocate(tensor.nbytes()));
        check(runtime_.copy_to(&buffer, tensor.data_ptr(), tensor.nbytes()));
        auto region = value_or_throw(transport_.endpoint()->reg_mr(buffer, client::MemoryAccess::DirectNpu));
        return {std::move(buffer), std::move(region), tensor};
    }
    client::Runtime *runtime() noexcept {
        return &runtime_;
    }
    client::Transport *transport_state() noexcept {
        return &transport_;
    }
    client::StorageClient *storage_state() noexcept {
        return &storage_;
    }

private:
    client::Runtime runtime_;
    client::Transport transport_;
    client::StorageClient storage_;
    bool storage_open_{};
};

std::vector<std::int64_t> Verbs::post_send(const ::torch::Tensor &input) {
    session_->require_ra("verbs");
    RegisteredTensor registered = session_->register_tensor(input);
    const nds_device_send_wr request{1U,
                                     NDS_DEVICE_WR_SEND,
                                     NDS_DEVICE_SEND_SIGNALED,
                                     {registered.region.address(), static_cast<std::uint32_t>(registered.region.length()),
                                      registered.region.local_key()},
                                     0U,
                                     0U,
                                     0U};
    const auto posted = value_or_throw(NdsRaPostSend(session_->transport_state()->qp(), request));
    sends_.push_back(std::move(registered));
    return {static_cast<std::int64_t>(posted.doorbell.db_index), static_cast<std::int64_t>(posted.doorbell.db_info)};
}

void Verbs::post_receive(const ::torch::Tensor &output) {
    session_->require_ra("verbs");
    RegisteredTensor registered = session_->register_tensor(output);
    const nds_device_recv_wr request{1U, {registered.region.address(), static_cast<std::uint32_t>(registered.region.length()),
                                          registered.region.local_key()}};
    check(NdsRaPostRecv(session_->transport_state()->qp(), request));
    receives_.push_back(std::move(registered));
}

std::int64_t Verbs::poll_send() {
    session_->require_ra("verbs");
    nds_device_completion_output output{};
    return static_cast<std::int64_t>(value_or_throw(
        NdsRaPollCq(session_->transport_state()->qp(), NDS_DEVICE_SEND_QUEUE, &output)));
}

std::int64_t Verbs::poll_receive() {
    session_->require_ra("verbs");
    nds_device_completion_output output{};
    const auto count = value_or_throw(NdsRaPollCq(session_->transport_state()->qp(), NDS_DEVICE_RECEIVE_QUEUE, &output));
    if (count > 0U) {
        for (auto &received : receives_)
            check(session_->runtime()->copy_from(received.tensor.data_ptr(), received.buffer, received.tensor.nbytes()));
        receives_.clear();
    }
    return static_cast<std::int64_t>(count);
}

void Transport::send(const ::torch::Tensor &input) {
    session_->require_ra("transport");
    RegisteredTensor registered = session_->register_tensor(input);
    const nds_device_transfer transfer{1U,
                                       {registered.region.address(), static_cast<std::uint32_t>(registered.region.length()),
                                        registered.region.local_key()},
                                       0U,
                                       0U,
                                       0U};
    check(NdsRaRdmaSend({session_->runtime(), session_->transport_state()->qp()}, transfer));
    sends_.push_back(std::move(registered));
}

void Storage::read_(const ::torch::Tensor &output, std::int64_t offset) {
    check_tensor(output);
    TORCH_CHECK(offset >= 0, "storage offset must be nonnegative");
    auto buffer = value_or_throw(session_->runtime()->allocate(output.nbytes()));
    check(session_->storage_state()->read(static_cast<std::uint64_t>(offset), &buffer,
                                          static_cast<std::uint32_t>(output.nbytes())));
    check(session_->runtime()->copy_from(output.data_ptr(), buffer, output.nbytes()));
}

void Storage::write(const ::torch::Tensor &input, std::int64_t offset) {
    check_tensor(input);
    TORCH_CHECK(offset >= 0, "storage offset must be nonnegative");
    auto buffer = value_or_throw(session_->runtime()->allocate(input.nbytes()));
    check(session_->runtime()->copy_to(&buffer, input.data_ptr(), input.nbytes()));
    check(session_->storage_state()->write(static_cast<std::uint64_t>(offset), &buffer,
                                           static_cast<std::uint32_t>(input.nbytes())));
}

std::int64_t Storage::capacity() const {
    return static_cast<std::int64_t>(session_->storage_state()->capacity());
}

}  // namespace
}  // namespace nds::torch

PYBIND11_MODULE(TORCH_EXTENSION_NAME, module) {
    namespace nds_torch = nds::torch;
    pybind11::class_<nds_torch::Session, std::shared_ptr<nds_torch::Session>>(module, "Session")
        .def(pybind11::init<std::string, std::string, std::string, std::string, std::int64_t, std::int64_t,
                            std::string, std::int64_t, std::string, std::string, std::string>(),
             pybind11::arg("ascendcl_library"), pybind11::arg("runtime_library"), pybind11::arg("ra_library"),
             pybind11::arg("npu_ip"), pybind11::arg("logical_device"), pybind11::arg("physical_device"),
             pybind11::arg("cpu_ip"), pybind11::arg("tcp_port"), pybind11::arg("backend") = "ra",
             pybind11::arg("aiv_kernel") = "", pybind11::arg("aicpu_kernel_config") = "")
        .def("verbs", &nds_torch::Session::verbs)
        .def("transport", &nds_torch::Session::transport)
        .def("storage", &nds_torch::Session::storage);
    pybind11::class_<nds_torch::Verbs, std::shared_ptr<nds_torch::Verbs>>(module, "Verbs")
        .def("post_send", &nds_torch::Verbs::post_send)
        .def("post_receive", &nds_torch::Verbs::post_receive)
        .def("poll_send", &nds_torch::Verbs::poll_send)
        .def("poll_receive", &nds_torch::Verbs::poll_receive);
    pybind11::class_<nds_torch::Transport, std::shared_ptr<nds_torch::Transport>>(module, "Transport")
        .def("send", &nds_torch::Transport::send);
    pybind11::class_<nds_torch::Storage, std::shared_ptr<nds_torch::Storage>>(module, "Storage")
        .def("read_", &nds_torch::Storage::read_)
        .def("write", &nds_torch::Storage::write)
        .def_property_readonly("capacity", &nds_torch::Storage::capacity);
}

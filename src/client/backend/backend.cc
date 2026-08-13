#include "backend.hh"

#include "nds/aicpu_roce.hh"
#include "nds/aiv_roce.hh"
#include "nds/host_ra.hh"
#include "nds/npu_ra_context.hh"

namespace nds {
namespace {

class DeviceAllocation {
public:
    explicit DeviceAllocation(NpuRaContext *context) : context_(context) {}
    ~DeviceAllocation() {
        if (address_ != nullptr && context_ != nullptr)
            (void)context_->free_device_memory(address_);
    }
    DeviceAllocation(const DeviceAllocation &) = delete;
    DeviceAllocation &operator=(const DeviceAllocation &) = delete;

    bool allocate(std::size_t size) {
        return context_ != nullptr && address_ == nullptr && context_->allocate_device_memory(size, &address_);
    }
    void *get() const noexcept {
        return address_;
    }

private:
    NpuRaContext *context_{};
    void *address_{};
};

}  // namespace

bool post_send(NpuRaContext *context, NpuRaQp *qp, const BackendConfig &config, std::uint64_t address,
               std::uint32_t length, std::uint32_t local_key, std::string *error) {
    if (context == nullptr || qp == nullptr || error == nullptr)
        return false;
    if (address == 0U || length == 0U || local_key == 0U) {
        *error = "Send requires registered source memory";
        return false;
    }
    if (config.mode == NpuBackendMode::HostRa) {
        return post_host_ra(context, qp, {{address, length, local_key}, NDS_RA_WR_SEND, 0U, 0U}, error);
    }
    if (config.mode == NpuBackendMode::Aicpu) {
        if (!qp->has_ai_qp_info()) {
            *error = "AICPU backend requires AI-QP metadata";
            return false;
        }
        AicpuRdmaPostLauncher launcher;
        if (!launcher.load(&context->acl_api(), config.aicpu_kernel_config) ||
            !launcher.launch_and_wait({NDS_AICPU_SEND, qp->ai_qp_info().ai_qp_address, local_key, 0U, address, 0U,
                                       length, 1U, context->logical_device_id()},
                                      5000)) {
            *error = launcher.error();
            return false;
        }
        return true;
    }
    if (!qp->has_ai_qp_info() || qp->ai_qp_info().data_plane_info == nullptr) {
        *error = "AIV backend requires AI send-queue metadata";
        return false;
    }
    const auto *plane = reinterpret_cast<const nds_ra_ai_data_plane_info *>(qp->ai_qp_info().data_plane_info);
    AivRdmaPostLauncher launcher;
    nds_aiv_rdma_post_request request{};
    if (!launcher.load(&context->acl_api(), config.aiv_kernel) ||
        !launcher.make_device_request(
            {plane->send_wq, qp->config().service_level, NDS_AIV_SEND, local_key, 0U, address, 0U, length, 1U},
            &request)) {
        *error = launcher.error();
        return false;
    }
    DeviceAllocation device_request(context);
    if (!device_request.allocate(sizeof(request)) ||
        !context->copy_host_to_device(device_request.get(), &request, sizeof(request)) ||
        !launcher.launch_post_and_wait(reinterpret_cast<std::uint64_t>(device_request.get()), 5000)) {
        *error = launcher.error().empty() ? context->error() : launcher.error();
        return false;
    }
    return true;
}

}  // namespace nds

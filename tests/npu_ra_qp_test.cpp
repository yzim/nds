#include "nds/npu_ra_qp.hpp"

#include <cassert>
#include <cstdint>
#include <cstring>

namespace {

struct FakeRaState {
    int rdev_init_calls{};
    int rdev_deinit_calls{};
    int qp_create_calls{};
    int qp_destroy_calls{};
    int get_attributes_calls{};
    int get_port_status_calls{};
    int port_status{NDS_RA_PORT_STATUS_ACTIVE};
    int get_port_status_result{};
    int get_support_lite_calls{};
    int support_lite{NDS_RA_LITE_ALIGN_4K};
    int get_support_lite_result{};
    int get_status_calls{};
    int status{NDS_RA_QP_STATUS_CONNECTED};
    int get_status_result{};
    int get_cqe_error_list_calls{};
    int get_cqe_error_list_result{};
    unsigned int cqe_error_count{};
    nds_ra_cqe_error cqe_errors[2]{};
    int modify_calls{};
    int register_mr_calls{};
    int deregister_mr_calls{};
    int send_wr_calls{};
    int poll_cq_calls{};
    int poll_result{};
    nds_ra_send_wr send_wr{};
    nds_ra_mr_info mr{};
    nds_ra_rdev rdev{};
    nds_ra_rdev_init_info rdev_init{};
    int rdev_mode{};
    unsigned int notify_type{};
    nds_ra_typical_qp local{};
    nds_ra_typical_qp remote{};
};

FakeRaState *state = nullptr;
int fake_rdev = 0;
int fake_qp = 0;

int fake_rdev_init(int mode, unsigned int notify_type, nds_ra_rdev rdev, void **handle)
{
    ++state->rdev_init_calls;
    state->rdev_mode = mode;
    state->notify_type = notify_type;
    state->rdev = rdev;
    *handle = &fake_rdev;
    return 0;
}

int fake_rdev_init_v2(nds_ra_rdev_init_info init, nds_ra_rdev rdev, void **handle)
{
    ++state->rdev_init_calls;
    state->rdev_init = init;
    state->rdev_mode = init.mode;
    state->notify_type = init.notify_type;
    state->rdev = rdev;
    *handle = &fake_rdev;
    return 0;
}

int fake_rdev_deinit(void *handle, unsigned int notify_type)
{
    assert(handle == &fake_rdev);
    assert(notify_type == NDS_RA_NOTIFY);
    ++state->rdev_deinit_calls;
    return 0;
}

int fake_qp_create(void *handle, int flag, int mode, nds_ra_typical_qp *initial, void **qp)
{
    assert(handle == &fake_rdev);
    assert(flag == NDS_RA_QP_FLAG_RC);
    assert(mode == NDS_RA_QP_MODE_OPBASE);
    ++state->qp_create_calls;
    initial->qpn = 1;
    *qp = &fake_qp;
    return 0;
}

int fake_qp_destroy(void *handle)
{
    assert(handle == &fake_qp);
    ++state->qp_destroy_calls;
    return 0;
}

int fake_get_attributes(void *handle, nds_ra_qp_attr *attributes)
{
    assert(handle == &fake_qp);
    ++state->get_attributes_calls;
    *attributes = {};
    attributes->qpn = 0x1234U;
    attributes->psn = 0x4567U;
    attributes->gid_index = 3U;
    attributes->gid[15] = 42U;
    return 0;
}

int fake_get_port_status(void *handle, int *status)
{
    assert(handle == &fake_rdev);
    assert(status != nullptr);
    ++state->get_port_status_calls;
    *status = state->port_status;
    return state->get_port_status_result;
}

int fake_get_support_lite(void *handle, int *support_lite)
{
    assert(handle == &fake_rdev);
    assert(support_lite != nullptr);
    ++state->get_support_lite_calls;
    *support_lite = state->support_lite;
    return state->get_support_lite_result;
}

int fake_get_status(void *handle, int *status)
{
    assert(handle == &fake_qp);
    assert(status != nullptr);
    ++state->get_status_calls;
    *status = state->status;
    return state->get_status_result;
}

int fake_get_cqe_error_list(void *handle, nds_ra_cqe_error *errors, unsigned int *count)
{
    assert(handle == &fake_rdev);
    assert(errors != nullptr);
    assert(count != nullptr);
    ++state->get_cqe_error_list_calls;
    if (state->get_cqe_error_list_result != 0) return state->get_cqe_error_list_result;
    assert(*count >= state->cqe_error_count);
    for (unsigned int index = 0; index < state->cqe_error_count; ++index) errors[index] = state->cqe_errors[index];
    *count = state->cqe_error_count;
    return 0;
}

int fake_modify(void *handle, nds_ra_typical_qp *local, nds_ra_typical_qp *remote)
{
    assert(handle == &fake_qp);
    ++state->modify_calls;
    state->local = *local;
    state->remote = *remote;
    return 0;
}

int fake_register_mr(const void *handle, nds_ra_mr_info *info, void **mr_handle)
{
    assert(handle == &fake_rdev);
    assert(info != nullptr);
    assert(mr_handle != nullptr);
    ++state->register_mr_calls;
    state->mr = *info;
    info->local_key = 0x1111U;
    info->remote_key = 0x2222U;
    *mr_handle = &state->mr;
    return 0;
}

int fake_deregister_mr(const void *handle, void *mr_handle)
{
    assert(handle == &fake_rdev);
    assert(mr_handle == &state->mr);
    ++state->deregister_mr_calls;
    return 0;
}

int fake_send_wr(void *handle, nds_ra_send_wr *wr, nds_ra_send_response *response)
{
    assert(handle == &fake_qp);
    assert(wr != nullptr && response != nullptr);
    ++state->send_wr_calls;
    state->send_wr = *wr;
    response->wqe.sq_index = 17U;
    response->wqe.wqe_index = 23U;
    return 0;
}

int fake_poll_cq(void *handle, bool is_send_cq, unsigned int max_entries, void *completions)
{
    assert(handle == &fake_qp);
    assert(is_send_cq);
    assert(max_entries == 1U);
    assert(completions != nullptr);
    ++state->poll_cq_calls;
    if (state->poll_result > 0) {
        static_cast<nds_ra_completion *>(completions)->status = 0;
    }
    return state->poll_result;
}

nds_ra_api make_fake_api()
{
    nds_ra_api api{};
    api.ra_rdev_init = fake_rdev_init;
    api.ra_rdev_init_v2 = fake_rdev_init_v2;
    api.ra_rdev_deinit = fake_rdev_deinit;
    api.ra_typical_qp_create = fake_qp_create;
    api.ra_qp_destroy = fake_qp_destroy;
    api.ra_get_qp_attr = fake_get_attributes;
    api.ra_rdev_get_port_status = fake_get_port_status;
    api.ra_rdev_get_support_lite = fake_get_support_lite;
    api.ra_get_qp_status = fake_get_status;
    api.ra_rdev_get_cqe_error_list = fake_get_cqe_error_list;
    api.ra_typical_qp_modify = fake_modify;
    api.ra_register_mr = fake_register_mr;
    api.ra_deregister_mr = fake_deregister_mr;
    api.ra_typical_send_wr = fake_send_wr;
    api.ra_poll_cq = fake_poll_cq;
    return api;
}

void test_create_advertise_connect_and_reset()
{
    FakeRaState fake{};
    state = &fake;
    nds_ra_api api = make_fake_api();
    nds::NpuRaQp qp;
    nds::NpuRaQpConfig config{};
    nds_rc_endpoint local{};
    nds_rc_endpoint peer{};

    config.local_ipv4 = "192.0.2.10";
    config.port_num = 1;
    config.path_mtu = 1024;
    config.traffic_class = 7;
    config.service_level = 2;
    config.retry_count = 6;
    config.retry_timeout = 13;

    assert(qp.create(api, config));
    assert(qp.created());
    int port_status = -1;
    assert(qp.query_port_status(port_status));
    assert(port_status == NDS_RA_PORT_STATUS_ACTIVE);
    assert(fake.get_port_status_calls == 1);
    int support_lite = -1;
    assert(qp.query_support_lite(support_lite));
    assert(support_lite == NDS_RA_LITE_ALIGN_4K);
    assert(fake.get_support_lite_calls == 1);
    assert(fake.rdev_init_calls == 1);
    assert(fake.rdev_mode == NDS_RA_NETWORK_OFFLINE);
    assert(fake.notify_type == NDS_RA_NOTIFY);
    assert(!fake.rdev_init.enabled_910a_lite);
    assert(fake.rdev_init.disabled_lite_thread);
    assert(!fake.rdev_init.enabled_2mb_lite);
    assert(fake.rdev.family == AF_INET);
    assert(fake.qp_create_calls == 1);
    assert(fake.get_attributes_calls == 1);

    assert(qp.make_qp_only_endpoint(local));
    assert(local.qp_num == 0x1234U);
    assert(local.psn == 0x4567U);
    assert(local.gid_index == 3U);
    assert(local.gid[15] == 42U);
    assert(local.flags == NDS_ENDPOINT_FLAG_QP_ONLY);
    assert(local.rkey == 0U);
    assert(local.address == 0U);
    assert(local.access_flags == 0U);
    assert(local.traffic_class == 7U);
    assert(local.service_level == 2U);

    peer.flags = NDS_ENDPOINT_FLAG_QP_ONLY;
    peer.qp_num = 0x2000U;
    peer.psn = 0x3000U;
    peer.port_num = 1;
    peer.path_mtu = 1024;
    peer.gid_index = 4;
    peer.gid[0] = 99U;
    peer.traffic_class = 9U;
    peer.service_level = 3U;
    peer.retry_count = 5U;
    peer.retry_timeout = 12U;
    assert(qp.connect(peer));
    assert(qp.connected());
    int status = -1;
    assert(qp.query_status(status));
    assert(status == NDS_RA_QP_STATUS_CONNECTED);
    assert(fake.get_status_calls == 1);
    assert(fake.modify_calls == 1);
    assert(fake.local.qpn == local.qp_num);
    assert(fake.local.psn == local.psn);
    assert(fake.local.gid_index == local.gid_index);
    assert(fake.local.traffic_class == 7U);
    assert(fake.local.service_level == 2U);
    assert(fake.remote.qpn == peer.qp_num);
    assert(fake.remote.psn == peer.psn);
    assert(fake.remote.gid_index == peer.gid_index);
    assert(fake.remote.gid[0] == 99U);
    assert(fake.remote.traffic_class == 9U);
    assert(fake.remote.service_level == 3U);

    qp.reset();
    assert(!qp.created());
    assert(!qp.connected());
    assert(fake.qp_destroy_calls == 1);
    assert(fake.rdev_deinit_calls == 1);
}

void test_memory_registration_lifecycle()
{
    FakeRaState fake{};
    state = &fake;
    nds_ra_api api = make_fake_api();
    nds::NpuRaQp qp;
    nds::NpuRaQpConfig config{};
    nds_ra_mr_info info{};
    void *mr_handle = nullptr;
    std::uint8_t buffer[64]{};

    config.local_ipv4 = "192.0.2.10";
    assert(qp.create(api, config));
    assert(qp.register_memory(buffer, sizeof(buffer), NDS_RA_ACCESS_DIRECT_NPU, info, &mr_handle));
    assert(fake.register_mr_calls == 1);
    assert(fake.mr.address == buffer);
    assert(fake.mr.size == sizeof(buffer));
    assert(fake.mr.access == NDS_RA_ACCESS_DIRECT_NPU);
    assert(info.local_key == 0x1111U);
    assert(info.remote_key == 0x2222U);
    assert(mr_handle == &fake.mr);
    assert(qp.deregister_memory(mr_handle));
    assert(fake.deregister_mr_calls == 1);
    qp.reset();
}

void test_send_wr_and_polling()
{
    FakeRaState fake{};
    state = &fake;
    nds_ra_api api = make_fake_api();
    nds::NpuRaQp qp;
    nds::NpuRaQpConfig config{};
    nds_rc_endpoint peer{};
    nds_ra_send_response response{};
    nds_ra_completion completion{};

    config.local_ipv4 = "192.0.2.10";
    assert(qp.create(api, config));
    assert(!qp.post_rdma_write({0x1000U, 64U, 0x99U}, 0x2000U, 0x88U, true, response));
    peer.flags = NDS_ENDPOINT_FLAG_QP_ONLY;
    peer.qp_num = 0x2000U;
    peer.psn = 0x3000U;
    peer.port_num = 1U;
    peer.path_mtu = 1024U;
    peer.gid_index = 3U;
    peer.retry_count = 7U;
    peer.retry_timeout = 14U;
    assert(qp.connect(peer));
    assert(qp.post_rdma_write({0x1000U, 64U, 0x99U}, 0x2000U, 0x88U, true, response));
    assert(fake.send_wr_calls == 1);
    assert(fake.send_wr.buffers != nullptr);
    assert(fake.send_wr.buffers->address == 0x1000U);
    assert(fake.send_wr.buffers->length == 64U);
    assert(fake.send_wr.buffers->local_key == 0x99U);
    assert(fake.send_wr.remote_address == 0x2000U);
    assert(fake.send_wr.remote_key == 0x88U);
    assert(fake.send_wr.opcode == NDS_RA_WR_RDMA_WRITE);
    assert(fake.send_wr.send_flags == NDS_RA_SEND_SIGNALED);
    assert(response.wqe.sq_index == 17U && response.wqe.wqe_index == 23U);
    fake.poll_result = 0;
    assert(qp.poll_send_completions(&completion, 1U) == 0);
    fake.poll_result = 1;
    assert(qp.poll_send_completions(&completion, 1U) == 1);
    fake.poll_result = -7;
    assert(qp.poll_send_completions(&completion, 1U) < 0);
    fake.cqe_error_count = 1U;
    fake.cqe_errors[0].status = 12U;
    fake.cqe_errors[0].qp_number = 0x1234U;
    nds_ra_cqe_error errors[2]{};
    std::uint32_t error_count = 2U;
    assert(qp.query_cqe_errors(errors, error_count));
    assert(fake.get_cqe_error_list_calls == 1);
    assert(error_count == 1U);
    assert(errors[0].status == 12U && errors[0].qp_number == 0x1234U);
    fake.get_cqe_error_list_result = -8;
    error_count = 2U;
    assert(!qp.query_cqe_errors(errors, error_count));
    qp.reset();
}

void test_rejects_invalid_configuration_and_endpoint()
{
    FakeRaState fake{};
    state = &fake;
    nds_ra_api api = make_fake_api();
    nds::NpuRaQp qp;
    nds::NpuRaQpConfig config{};
    nds_rc_endpoint endpoint{};
    nds_rc_endpoint invalid_peer{};

    assert(!qp.create(api, config));
    assert(!qp.error().empty());
    config.local_ipv4 = "192.0.2.10";
    assert(qp.create(api, config));
    int status = -1;
    assert(qp.query_status(status));
    assert(status == NDS_RA_QP_STATUS_CONNECTED);
    fake.port_status = 2;
    assert(!qp.query_port_status(status));
    assert(!qp.error().empty());
    fake.port_status = NDS_RA_PORT_STATUS_ACTIVE;
    fake.get_port_status_result = -10;
    assert(!qp.query_port_status(status));
    assert(!qp.error().empty());
    fake.get_port_status_result = 0;
    fake.support_lite = 3;
    assert(!qp.query_support_lite(status));
    assert(!qp.error().empty());
    fake.support_lite = NDS_RA_LITE_ALIGN_4K;
    fake.get_support_lite_result = -11;
    assert(!qp.query_support_lite(status));
    assert(!qp.error().empty());
    fake.get_support_lite_result = 0;
    fake.status = 4;
    assert(!qp.query_status(status));
    assert(!qp.error().empty());
    fake.status = NDS_RA_QP_STATUS_CONNECTED;
    fake.get_status_result = -9;
    assert(!qp.query_status(status));
    assert(!qp.error().empty());
    assert(qp.make_qp_only_endpoint(endpoint));
    assert(!qp.make_data_ready_endpoint(0, 0, endpoint));
    assert(!qp.connect(invalid_peer));
    qp.reset();
}

} // namespace

int main()
{
    test_create_advertise_connect_and_reset();
    test_memory_registration_lifecycle();
    test_send_wr_and_polling();
    test_rejects_invalid_configuration_and_endpoint();
    return 0;
}

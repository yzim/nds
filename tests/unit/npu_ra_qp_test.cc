#include "ra.hh"
#include "nds/npu_ra_qp.hh"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>

namespace {

struct FakeRaState {
    int rdev_init_calls{};
    int rdev_deinit_calls{};
    int qp_create_calls{};
    int ai_qp_create_calls{};
    int set_qos_calls{};
    int set_timeout_calls{};
    int set_retry_count_calls{};
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
    int recv_wr_calls{};
    int poll_cq_calls{};
    int poll_result{};
    bool poll_send_cq{};
    nds_ra_send_wr send_wr{};
    nds_ra_sge send_sge{};
    nds_ra_recv_wr recv_wr{};
    nds_ra_mr_info mr{};
    nds_ra_rdev rdev{};
    nds_ra_rdev_init_info rdev_init{};
    int rdev_mode{};
    unsigned int notify_type{};
    nds_ra_typical_qp local{};
    nds_ra_typical_qp remote{};
    nds_ra_qp_ext_attrs ai_qp_attrs{};
    nds_ra_ai_qp_info ai_qp_info{};
    nds_ra_qos_attr qos{};
    uint32_t timeout{};
    uint32_t retry_count{};
    bool omit_work_queues{};
    bool omit_completion_queues{};
};

FakeRaState *state = nullptr;
int fake_rdev = 0;
int fake_qp = 0;

int fake_rdev_init(int mode, unsigned int notify_type, nds_ra_rdev rdev, void **handle) {
    ++state->rdev_init_calls;
    state->rdev_mode = mode;
    state->notify_type = notify_type;
    state->rdev = rdev;
    *handle = &fake_rdev;
    return 0;
}

int fake_rdev_init_v2(nds_ra_rdev_init_info init, nds_ra_rdev rdev, void **handle) {
    ++state->rdev_init_calls;
    state->rdev_init = init;
    state->rdev_mode = init.mode;
    state->notify_type = init.notify_type;
    state->rdev = rdev;
    *handle = &fake_rdev;
    return 0;
}

int fake_rdev_deinit(void *handle, unsigned int notify_type) {
    assert(handle == &fake_rdev);
    assert(notify_type == NDS_RA_NOTIFY);
    ++state->rdev_deinit_calls;
    return 0;
}

int fake_qp_create(void *handle, int flag, int mode, nds_ra_typical_qp *initial, void **qp) {
    assert(handle == &fake_rdev);
    assert(flag == NDS_RA_QP_FLAG_RC);
    assert(mode == NDS_RA_QP_MODE_OPBASE);
    ++state->qp_create_calls;
    initial->qpn = 1;
    *qp = &fake_qp;
    return 0;
}

int fake_ai_qp_create(void *handle, nds_ra_qp_ext_attrs *attrs, nds_ra_ai_qp_info *info, void **qp) {
    assert(handle == &fake_rdev);
    assert(attrs != nullptr);
    assert(info != nullptr);
    assert(qp != nullptr);
    ++state->ai_qp_create_calls;
    state->ai_qp_attrs = *attrs;
    *info = {};
    info->ai_qp_address = UINT64_C(0x123456789abcdef0);
    info->sq_index = 17U;
    info->db_index = 19U;
    info->ai_scq_address = UINT64_C(0x3000);
    info->ai_rcq_address = UINT64_C(0x4000);
    auto *plane = reinterpret_cast<nds_ra_ai_data_plane_info *>(info->data_plane_info);
    if (!state->omit_work_queues) {
        plane->send_wq = {1U, 0U, 0x10000U, 64U, 32768U, 0x11000U, 0x12000U,
                          0x13000U, 0x14000U, {}};
        plane->receive_wq = {1U, 0U, 0x20000U, 16U, 128U, 0x21000U, 0x22000U,
                             0x23000U, 0x24000U, {}};
    }
    if (!state->omit_completion_queues) {
        plane->send_cq = {2U, 0U, 0x30000U, 64U, 32768U, 0U, 0x32000U, 0x33000U,
                          0x34000U, {}};
        plane->receive_cq = {3U, 0U, 0x40000U, 64U, 128U, 0U, 0x42000U, 0x43000U,
                             0x44000U, {}};
    }
    state->ai_qp_info = *info;
    *qp = &fake_qp;
    return 0;
}

int fake_set_qos(void *handle, nds_ra_qos_attr *qos) {
    assert(handle == &fake_qp);
    assert(qos != nullptr);
    ++state->set_qos_calls;
    state->qos = *qos;
    return 0;
}

int fake_set_timeout(void *handle, uint32_t *timeout) {
    assert(handle == &fake_qp);
    assert(timeout != nullptr);
    ++state->set_timeout_calls;
    state->timeout = *timeout;
    return 0;
}

int fake_set_retry_count(void *handle, uint32_t *retry_count) {
    assert(handle == &fake_qp);
    assert(retry_count != nullptr);
    ++state->set_retry_count_calls;
    state->retry_count = *retry_count;
    return 0;
}

int fake_qp_destroy(void *handle) {
    assert(handle == &fake_qp);
    ++state->qp_destroy_calls;
    return 0;
}

int fake_get_attributes(void *handle, nds_ra_qp_attr *attributes) {
    assert(handle == &fake_qp);
    ++state->get_attributes_calls;
    *attributes = {};
    attributes->qpn = 0x1234U;
    attributes->psn = 0x4567U;
    attributes->gid_index = 3U;
    attributes->gid[15] = 42U;
    return 0;
}

int fake_get_port_status(void *handle, int *status) {
    assert(handle == &fake_rdev);
    assert(status != nullptr);
    ++state->get_port_status_calls;
    *status = state->port_status;
    return state->get_port_status_result;
}

int fake_get_support_lite(void *handle, int *support_lite) {
    assert(handle == &fake_rdev);
    assert(support_lite != nullptr);
    ++state->get_support_lite_calls;
    *support_lite = state->support_lite;
    return state->get_support_lite_result;
}

int fake_get_status(void *handle, int *status) {
    assert(handle == &fake_qp);
    assert(status != nullptr);
    ++state->get_status_calls;
    *status = state->status;
    return state->get_status_result;
}

int fake_get_cqe_error_list(void *handle, nds_ra_cqe_error *errors, unsigned int *count) {
    assert(handle == &fake_rdev);
    assert(errors != nullptr);
    assert(count != nullptr);
    ++state->get_cqe_error_list_calls;
    if (state->get_cqe_error_list_result != 0)
        return state->get_cqe_error_list_result;
    assert(*count >= state->cqe_error_count);
    for (unsigned int index = 0; index < state->cqe_error_count; ++index) errors[index] = state->cqe_errors[index];
    *count = state->cqe_error_count;
    return 0;
}

int fake_modify(void *handle, nds_ra_typical_qp *local, nds_ra_typical_qp *remote) {
    assert(handle == &fake_qp);
    ++state->modify_calls;
    state->local = *local;
    state->remote = *remote;
    return 0;
}

int fake_register_mr(const void *handle, nds_ra_mr_info *info, void **mr_handle) {
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

int fake_deregister_mr(const void *handle, void *mr_handle) {
    assert(handle == &fake_rdev);
    assert(mr_handle == &state->mr);
    ++state->deregister_mr_calls;
    return 0;
}

int fake_send_wr(void *handle, nds_ra_send_wr *wr, nds_ra_send_response *response) {
    assert(handle == &fake_qp);
    assert(wr != nullptr && response != nullptr);
    ++state->send_wr_calls;
    state->send_wr = *wr;
    if (wr->buffers != nullptr) {
        state->send_sge = *wr->buffers;
        state->send_wr.buffers = &state->send_sge;
    }
    response->wqe.sq_index = 17U;
    response->wqe.wqe_index = 23U;
    return 0;
}

int fake_recv_wrlist(void *handle, nds_ra_recv_wr *wr, unsigned int recv_num, unsigned int *complete_num) {
    assert(handle == &fake_qp);
    assert(wr != nullptr && recv_num == 1U && complete_num != nullptr);
    ++state->recv_wr_calls;
    state->recv_wr = *wr;
    *complete_num = 1U;
    return 0;
}

int fake_poll_cq(void *handle, bool is_send_cq, unsigned int max_entries, void *completions) {
    assert(handle == &fake_qp);
    assert(max_entries == NDS_DEVICE_MAX_COMPLETIONS);
    assert(completions != nullptr);
    ++state->poll_cq_calls;
    state->poll_send_cq = is_send_cq;
    if (state->poll_result > 0) {
        static_cast<nds_ra_completion *>(completions)->status = 0;
    }
    return state->poll_result;
}

nds_ra_api make_fake_api() {
    nds_ra_api api{};
    api.ra_rdev_init = fake_rdev_init;
    api.ra_rdev_init_v2 = fake_rdev_init_v2;
    api.ra_rdev_deinit = fake_rdev_deinit;
    api.ra_typical_qp_create = fake_qp_create;
    api.ra_ai_qp_create = fake_ai_qp_create;
    api.ra_set_qp_attr_qos = fake_set_qos;
    api.ra_set_qp_attr_timeout = fake_set_timeout;
    api.ra_set_qp_attr_retry_count = fake_set_retry_count;
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
    api.ra_recv_wrlist = fake_recv_wrlist;
    api.ra_poll_cq = fake_poll_cq;
    return api;
}

void test_create_advertise_connect_and_reset() {
    FakeRaState fake{};
    state = &fake;
    nds_ra_api api = make_fake_api();
    nds::NpuRaQp qp;
    nds::NpuRaQpConfig config{};
    nds_qp_info local{};
    nds_qp_info peer{};

    config.local_ipv4 = "192.0.2.10";
    config.port_num = 1;
    config.path_mtu = 1024;
    config.traffic_class = 7;
    config.service_level = 2;
    config.retry_count = 6;
    config.retry_timeout = 13;

    assert(qp.create(&api, config));
    assert(qp.created());
    int port_status = -1;
    assert(qp.query_port_status(&port_status));
    assert(port_status == NDS_RA_PORT_STATUS_ACTIVE);
    assert(fake.get_port_status_calls == 1);
    int support_lite = -1;
    assert(qp.query_support_lite(&support_lite));
    assert(support_lite == NDS_RA_LITE_ALIGN_4K);
    assert(fake.get_support_lite_calls == 1);
    assert(fake.rdev_init_calls == 1);
    assert(fake.rdev_mode == NDS_RA_NETWORK_OFFLINE);
    assert(fake.notify_type == NDS_RA_NOTIFY);
    assert(!fake.rdev_init.enabled_910a_lite);
    assert(!fake.rdev_init.disabled_lite_thread);
    assert(!fake.rdev_init.enabled_2mb_lite);
    assert(fake.rdev.family == AF_INET);
    assert(fake.qp_create_calls == 1);
    assert(fake.get_attributes_calls == 1);

    assert(qp.make_qp_info(&local));
    assert(local.qp_num == 0x1234U);
    assert(local.psn == 0x4567U);
    assert(local.gid_index == 3U);
    assert(local.gid[15] == 42U);
    assert(local.traffic_class == 7U);
    assert(local.service_level == 2U);

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
    assert(qp.query_status(&status));
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

void test_aicpu_qp_creation_and_connection() {
    FakeRaState fake{};
    state = &fake;
    nds_ra_api api = make_fake_api();
    nds::NpuRaQp qp;
    nds::NpuRaQpConfig config{};
    nds_qp_info local{};
    nds_qp_info peer{};
    nds_device_completion_output completions{};

    config.local_ipv4 = "192.0.2.10";
    assert(qp.create(&api, config, nds::NpuExecutionMode::Aicpu));
    assert(qp.execution_mode() == nds::NpuExecutionMode::Aicpu);
    assert(!fake.rdev_init.disabled_lite_thread);
    assert(fake.qp_create_calls == 0);
    assert(fake.ai_qp_create_calls == 1);
    assert(fake.ai_qp_attrs.qp_mode == NDS_RA_QP_MODE_NORMAL);
    assert(fake.ai_qp_attrs.cq_attr.send_cq_depth == 32768);
    assert(fake.ai_qp_attrs.cq_attr.recv_cq_depth == 128);
    assert(fake.ai_qp_attrs.qp_attr.cap.max_send_wr == 32768U);
    assert(fake.ai_qp_attrs.qp_attr.cap.max_recv_wr == 128U);
    assert(fake.ai_qp_attrs.qp_attr.cap.max_send_sge == 1U);
    assert(fake.ai_qp_attrs.qp_attr.cap.max_recv_sge == 1U);
    assert(fake.ai_qp_attrs.qp_attr.cap.max_inline_data == 32U);
    assert(fake.ai_qp_attrs.qp_attr.qp_type == NDS_RA_QP_TYPE_RC);
    assert(fake.ai_qp_attrs.version == NDS_RA_QP_CREATE_WITH_ATTR_VERSION);
    assert(fake.ai_qp_attrs.data_plane_flag == NDS_RA_AI_CALLER_POLLS_CQ);
    assert(fake.set_qos_calls == 1);
    assert(fake.qos.traffic_class == 0U);
    assert(fake.qos.service_level == 0U);
    assert(fake.set_timeout_calls == 1);
    assert(fake.timeout == 14U);
    assert(fake.set_retry_count_calls == 1);
    assert(fake.retry_count == 7U);
    assert(qp.has_ai_qp_info());
    assert(qp.ai_qp_info().ai_qp_address == UINT64_C(0x123456789abcdef0));
    assert(qp.ai_qp_info().sq_index == 17U);
    assert(qp.ai_qp_info().db_index == 19U);
    assert(qp.set_device_wr_id_storage(0x50000U, 0x60000U));
    const auto transport = qp.make_device_transport();
    assert(transport);
    assert(transport->abi_version == NDS_DEVICE_TRANSPORT_ABI_VERSION);
    assert(transport->control_qp.provider_qp_address == UINT64_C(0x123456789abcdef0));
    assert(transport->control_qp.qp_mode == NDS_RA_QP_MODE_NORMAL);
    assert(transport->control_qp.send_queue.doorbell_mode == NDS_DEVICE_DOORBELL_MMIO);
    assert(transport->control_qp.send_queue.doorbell_address == 0x14000U);
    assert(transport->control_qp.receive_queue.doorbell_mode == NDS_DEVICE_DOORBELL_RECORD);
    assert(transport->control_qp.receive_queue.doorbell_address == 0x23000U);
    assert(transport->control_qp.send_cq.doorbell_address == 0x33000U);

    assert(qp.make_qp_info(&local));
    peer.qp_num = 0x2000U;
    peer.psn = 0x3000U;
    peer.port_num = 1U;
    peer.path_mtu = 1024U;
    peer.gid_index = 3U;
    peer.retry_count = 7U;
    peer.retry_timeout = 14U;
    assert(qp.connect(peer));
    assert(fake.modify_calls == 1);
    assert(!nds::NdsRaPostSend(&qp, {1U, NDS_DEVICE_WR_SEND, NDS_DEVICE_SEND_SIGNALED,
                                     {0x1000U, 64U, 0x99U}, 0U, 0U, 0U}));
    assert(!nds::NdsRaPollCq(&qp, NDS_DEVICE_SEND_QUEUE, &completions));
    assert(fake.send_wr_calls == 0);
    assert(fake.poll_cq_calls == 0);
    qp.reset();
    assert(qp.execution_mode() == nds::NpuExecutionMode::Ra);
    assert(!qp.has_ai_qp_info());
    assert(qp.ai_qp_info().ai_qp_address == 0U);
    assert(fake.qp_destroy_calls == 1);
    assert(fake.rdev_deinit_calls == 1);
}

void test_aiv_uses_opbase_ext_qp() {
    FakeRaState fake{};
    state = &fake;
    nds_ra_api api = make_fake_api();
    nds::NpuRaQp qp;
    nds::NpuRaQpConfig config{};

    config.local_ipv4 = "192.0.2.10";
    assert(qp.create(&api, config, nds::NpuExecutionMode::Aiv));
    assert(fake.ai_qp_create_calls == 1);
    assert(fake.ai_qp_attrs.qp_mode == NDS_RA_QP_MODE_OPBASE_EXT);
    qp.reset();
}

void test_aiv_qp_mode_override_and_hccp_owned_cq() {
    FakeRaState fake{};
    fake.omit_completion_queues = true;
    state = &fake;
    nds_ra_api api = make_fake_api();
    nds::NpuRaQp qp;
    nds::NpuRaQpConfig config{};
    config.local_ipv4 = "192.0.2.10";
    config.ai_qp_mode = NDS_RA_QP_MODE_OPBASE_EXT;
    config.send_queue_depth = 64U;
    config.receive_queue_depth = 32U;
    config.control_flags = 0U;
    assert(qp.create(&api, config, nds::NpuExecutionMode::Aiv));
    assert(fake.ai_qp_attrs.qp_mode == NDS_RA_QP_MODE_OPBASE_EXT);
    assert(fake.ai_qp_attrs.cq_attr.send_cq_depth == 64);
    assert(fake.ai_qp_attrs.cq_attr.recv_cq_depth == 32);
    assert(fake.ai_qp_attrs.data_plane_flag == 0U);
    assert(qp.set_device_wr_id_storage(0x50000U, 0x60000U));
    const auto transport = qp.make_device_transport();
    assert(transport);
    assert(transport->control_qp.flags == 0U);
    assert(transport->control_qp.send_cq.buffer_address == 0U);
    assert(transport->control_qp.receive_cq.buffer_address == 0U);
}

void test_aicpu_rejects_non_normal_qp() {
    FakeRaState fake{};
    state = &fake;
    nds_ra_api api = make_fake_api();
    nds::NpuRaQp qp;
    nds::NpuRaQpConfig config{};
    config.local_ipv4 = "192.0.2.10";
    config.ai_qp_mode = NDS_RA_QP_MODE_OPBASE_EXT;

    assert(!qp.create(&api, config, nds::NpuExecutionMode::Aicpu));
    assert(qp.error().find("NORMAL AI QP") != std::string::npos);
    assert(fake.rdev_init_calls == 0);
    assert(fake.ai_qp_create_calls == 0);
}

void test_rejects_incomplete_ai_connection() {
    FakeRaState fake{};
    fake.omit_work_queues = true;
    state = &fake;
    nds_ra_api api = make_fake_api();
    nds::NpuRaQp qp;
    nds::NpuRaQpConfig config{};
    config.local_ipv4 = "192.0.2.10";
    assert(qp.create(&api, config, nds::NpuExecutionMode::Aiv));
    assert(qp.set_device_wr_id_storage(0x50000U, 0x60000U));
    assert(!qp.make_device_transport());
    qp.reset();

    fake = {};
    fake.omit_completion_queues = true;
    state = &fake;
    api = make_fake_api();
    assert(qp.create(&api, config, nds::NpuExecutionMode::Aiv));
    assert(qp.set_device_wr_id_storage(0x50000U, 0x60000U));
    assert(!qp.make_device_transport());
}

void test_memory_registration_lifecycle() {
    FakeRaState fake{};
    state = &fake;
    nds_ra_api api = make_fake_api();
    nds::NpuRaQp qp;
    nds::NpuRaQpConfig config{};
    nds_ra_mr_info info{};
    void *mr_handle = nullptr;
    std::uint8_t buffer[64]{};

    config.local_ipv4 = "192.0.2.10";
    assert(qp.create(&api, config));
    assert(qp.register_memory(buffer, sizeof(buffer), NDS_RA_ACCESS_DIRECT_NPU, &info, &mr_handle));
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

void test_send_wr_and_polling() {
    FakeRaState fake{};
    state = &fake;
    nds_ra_api api = make_fake_api();
    nds::NpuRaQp qp;
    nds::NpuRaQpConfig config{};
    nds_qp_info peer{};
    nds_device_completion_output completions{};
    const nds_device_send_wr write{1U, NDS_DEVICE_WR_RDMA_WRITE, NDS_DEVICE_SEND_SIGNALED,
                                   {0x1000U, 64U, 0x99U}, 0x2000U, 0x88U, 0U};
    const nds_device_send_wr send{2U, NDS_DEVICE_WR_SEND, NDS_DEVICE_SEND_SIGNALED,
                                  {0x1000U, 64U, 0x99U}, 0U, 0U, 0U};

    config.local_ipv4 = "192.0.2.10";
    assert(qp.create(&api, config));
    assert(!nds::NdsRaPostSend(&qp, write));
    peer.qp_num = 0x2000U;
    peer.psn = 0x3000U;
    peer.port_num = 1U;
    peer.path_mtu = 1024U;
    peer.gid_index = 3U;
    peer.retry_count = 7U;
    peer.retry_timeout = 14U;
    assert(qp.connect(peer));
    const auto posted = nds::NdsRaPostSend(&qp, write);
    assert(posted);
    assert(fake.send_wr_calls == 1);
    assert(fake.send_wr.buffers != nullptr);
    assert(fake.send_wr.buffers->address == 0x1000U);
    assert(fake.send_wr.buffers->length == 64U);
    assert(fake.send_wr.buffers->local_key == 0x99U);
    assert(fake.send_wr.remote_address == 0x2000U);
    assert(fake.send_wr.remote_key == 0x88U);
    assert(fake.send_wr.opcode == NDS_RA_WR_RDMA_WRITE);
    assert(fake.send_wr.send_flags == NDS_RA_SEND_SIGNALED);
    assert(posted->wqe.sq_index == 17U && posted->wqe.wqe_index == 23U);
    assert(!nds::NdsRaPostSend(&qp, {2U, NDS_DEVICE_WR_SEND, NDS_DEVICE_SEND_SIGNALED,
                                     {0x1000U, 64U, 0x99U}, 0x2000U, 0x88U, 0U}));
    assert(nds::NdsRaPostSend(&qp, send));
    assert(fake.send_wr_calls == 2);
    assert(fake.send_wr.remote_address == 0U);
    assert(fake.send_wr.remote_key == 0U);
    assert(fake.send_wr.opcode == NDS_RA_WR_SEND);
    assert(nds::NdsRaPostRecv(&qp, {3U, {0x3000U, 128U, 0x77U}}));
    assert(fake.recv_wr_calls == 1);
    assert(fake.recv_wr.wr_id == 3U);
    assert(fake.recv_wr.memory.address == 0x3000U);
    assert(fake.recv_wr.memory.length == 128U);
    assert(fake.recv_wr.memory.local_key == 0x77U);
    fake.poll_result = 0;
    const auto empty = nds::NdsRaPollCq(&qp, NDS_DEVICE_SEND_QUEUE, &completions);
    assert(empty && *empty == 0U);
    assert(fake.poll_send_cq);
    fake.poll_result = 1;
    const auto one = nds::NdsRaPollCq(&qp, NDS_DEVICE_RECEIVE_QUEUE, &completions);
    assert(one && *one == 1U);
    assert(!fake.poll_send_cq);
    assert(completions.count == 1U);
    fake.poll_result = 100007;
    assert(!nds::NdsRaPollCq(&qp, NDS_DEVICE_SEND_QUEUE, &completions));
    fake.poll_result = -7;
    assert(!nds::NdsRaPollCq(&qp, NDS_DEVICE_SEND_QUEUE, &completions));
    fake.cqe_error_count = 1U;
    fake.cqe_errors[0].status = 12U;
    fake.cqe_errors[0].qp_number = 0x1234U;
    nds_ra_cqe_error errors[2]{};
    std::uint32_t error_count = 2U;
    assert(qp.query_cqe_errors(errors, &error_count));
    assert(fake.get_cqe_error_list_calls == 1);
    assert(error_count == 1U);
    assert(errors[0].status == 12U && errors[0].qp_number == 0x1234U);
    fake.get_cqe_error_list_result = -8;
    error_count = 2U;
    assert(!qp.query_cqe_errors(errors, &error_count));
    qp.reset();
}

void test_rejects_invalid_configuration_and_endpoint() {
    FakeRaState fake{};
    state = &fake;
    nds_ra_api api = make_fake_api();
    nds::NpuRaQp qp;
    nds::NpuRaQpConfig config{};
    nds_qp_info endpoint{};
    nds_qp_info invalid_peer{};

    assert(!qp.create(&api, config));
    assert(!qp.error().empty());
    config.local_ipv4 = "192.0.2.10";
    config.send_queue_depth = 3U;
    assert(!qp.create(&api, config));
    config.send_queue_depth = 32768U;
    config.receive_queue_depth = 1U;
    assert(!qp.create(&api, config));
    config.receive_queue_depth = 128U;
    assert(!qp.create(&api, config, static_cast<nds::NpuExecutionMode>(99)));
    assert(!qp.error().empty());
    assert(qp.create(&api, config));
    int status = -1;
    assert(qp.query_status(&status));
    assert(status == NDS_RA_QP_STATUS_CONNECTED);
    fake.port_status = 2;
    assert(!qp.query_port_status(&status));
    assert(!qp.error().empty());
    fake.port_status = NDS_RA_PORT_STATUS_ACTIVE;
    fake.get_port_status_result = -10;
    assert(!qp.query_port_status(&status));
    assert(!qp.error().empty());
    fake.get_port_status_result = 0;
    fake.support_lite = 3;
    assert(!qp.query_support_lite(&status));
    assert(!qp.error().empty());
    fake.support_lite = NDS_RA_LITE_ALIGN_4K;
    fake.get_support_lite_result = -11;
    assert(!qp.query_support_lite(&status));
    assert(!qp.error().empty());
    fake.get_support_lite_result = 0;
    fake.status = 4;
    assert(!qp.query_status(&status));
    assert(!qp.error().empty());
    fake.status = NDS_RA_QP_STATUS_CONNECTED;
    fake.get_status_result = -9;
    assert(!qp.query_status(&status));
    assert(!qp.error().empty());
    assert(qp.make_qp_info(&endpoint));
    assert(!qp.connect(invalid_peer));
    qp.reset();
}

}  // namespace

int main() {
    test_create_advertise_connect_and_reset();
    test_aicpu_qp_creation_and_connection();
    test_aiv_uses_opbase_ext_qp();
    test_aiv_qp_mode_override_and_hccp_owned_cq();
    test_aicpu_rejects_non_normal_qp();
    test_rejects_incomplete_ai_connection();
    test_memory_registration_lifecycle();
    test_send_wr_and_polling();
    test_rejects_invalid_configuration_and_endpoint();
    return 0;
}

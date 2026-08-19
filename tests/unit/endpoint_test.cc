#include "endpoint.hh"
#include "ra.hh"
#include "runtime.hh"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace nds::client {

struct EndpointTestAccess {
    static void adopt(Endpoint *endpoint, const nds_ra_api &api, void *rdev) {
        endpoint->runtime_ = reinterpret_cast<Runtime *>(endpoint);
        endpoint->api_ = api;
        endpoint->rdev_handle_ = rdev;
    }

    static void make_host_buffer(MemoryBuffer *buffer, std::size_t size) {
        buffer->data_ = new std::byte[size];
        buffer->size_ = size;
        buffer->location_ = MemoryLocation::Host;
    }
};

}  // namespace nds::client

namespace {

struct FakeRa {
    int qp_create_calls{};
    int ai_qp_create_calls{};
    int qp_destroy_calls{};
    int modify_calls{};
    int register_calls{};
    int deregister_calls{};
    int send_calls{};
    int recv_calls{};
    int poll_calls{};
    int port_status{NDS_RA_PORT_STATUS_ACTIVE};
    int qp_status{NDS_RA_QP_STATUS_CONNECTED};
    int lite_support{NDS_RA_LITE_ALIGN_4K};
    nds_ra_qp_ext_attrs ai_attributes{};
    nds_ra_typical_qp local{};
    nds_ra_typical_qp remote{};
    nds_ra_mr_info mr{};
    nds_ra_send_wr send{};
    nds_ra_sge send_sge{};
};

FakeRa *fake_state{};
int fake_rdev{};
int fake_qp{};

int fake_rdev_deinit(void *handle, unsigned int notify_type) {
    EXPECT_EQ(handle, &fake_rdev);
    EXPECT_EQ(notify_type, NDS_RA_NOTIFY);
    return 0;
}

int fake_qp_create(void *rdev, int flag, int mode, nds_ra_typical_qp *, void **handle) {
    EXPECT_EQ(rdev, &fake_rdev);
    EXPECT_EQ(flag, NDS_RA_QP_FLAG_RC);
    EXPECT_EQ(mode, NDS_RA_QP_MODE_OPBASE);
    ++fake_state->qp_create_calls;
    *handle = &fake_qp;
    return 0;
}

int fake_ai_qp_create(void *rdev, nds_ra_qp_ext_attrs *attributes, nds_ra_ai_qp_info *info, void **handle) {
    EXPECT_EQ(rdev, &fake_rdev);
    ++fake_state->ai_qp_create_calls;
    fake_state->ai_attributes = *attributes;
    info->ai_qp_address = 0x1000U;
    info->ai_scq_address = 0x2000U;
    info->ai_rcq_address = 0x3000U;
    auto *plane = reinterpret_cast<nds_ra_ai_data_plane_info *>(info->data_plane_info);
    plane->send_wq = {1U, 0U, 0x10000U, 64U, 64U, 0x11000U, 0x12000U, 0x13000U, 0x14000U, {}};
    plane->receive_wq = {2U, 0U, 0x20000U, 16U, 32U, 0x21000U, 0x22000U, 0x23000U, 0x24000U, {}};
    plane->send_cq = {3U, 0U, 0x30000U, 64U, 64U, 0U, 0x32000U, 0x33000U, 0x34000U, {}};
    plane->receive_cq = {4U, 0U, 0x40000U, 64U, 32U, 0U, 0x42000U, 0x43000U, 0x44000U, {}};
    *handle = &fake_qp;
    return 0;
}

int fake_set_qos(void *, nds_ra_qos_attr *) {
    return 0;
}

int fake_set_u32(void *, std::uint32_t *) {
    return 0;
}

int fake_qp_destroy(void *handle) {
    EXPECT_EQ(handle, &fake_qp);
    ++fake_state->qp_destroy_calls;
    return 0;
}

int fake_get_qp_attr(void *handle, nds_ra_qp_attr *attributes) {
    EXPECT_EQ(handle, &fake_qp);
    attributes->qpn = 0x1234U;
    attributes->psn = 0x4567U;
    attributes->gid_index = 3U;
    attributes->gid[15] = 42U;
    return 0;
}

int fake_modify(void *handle, nds_ra_typical_qp *local, nds_ra_typical_qp *remote) {
    EXPECT_EQ(handle, &fake_qp);
    ++fake_state->modify_calls;
    fake_state->local = *local;
    fake_state->remote = *remote;
    return 0;
}

int fake_port_status(void *, int *status) {
    *status = fake_state->port_status;
    return 0;
}

int fake_qp_status(void *, int *status) {
    *status = fake_state->qp_status;
    return 0;
}

int fake_lite_support(void *, int *support) {
    *support = fake_state->lite_support;
    return 0;
}

int fake_cqe_errors(void *, nds_ra_cqe_error *, unsigned int *count) {
    *count = 0U;
    return 0;
}

int fake_register_mr(const void *rdev, nds_ra_mr_info *info, void **handle) {
    EXPECT_EQ(rdev, &fake_rdev);
    ++fake_state->register_calls;
    info->local_key = 0x1111U;
    info->remote_key = 0x2222U;
    fake_state->mr = *info;
    *handle = &fake_state->mr;
    return 0;
}

int fake_deregister_mr(const void *rdev, void *handle) {
    EXPECT_EQ(rdev, &fake_rdev);
    EXPECT_EQ(handle, &fake_state->mr);
    ++fake_state->deregister_calls;
    return 0;
}

int fake_send(void *, nds_ra_send_wr *request, nds_ra_send_response *response) {
    ++fake_state->send_calls;
    fake_state->send = *request;
    fake_state->send_sge = *request->buffers;
    fake_state->send.buffers = &fake_state->send_sge;
    response->wqe.sq_index = 17U;
    response->wqe.wqe_index = 23U;
    return 0;
}

int fake_recv(void *, nds_ra_recv_wr *, unsigned int count, unsigned int *completed) {
    ++fake_state->recv_calls;
    *completed = count;
    return 0;
}

int fake_poll(void *, bool, unsigned int, void *) {
    ++fake_state->poll_calls;
    return 0;
}

nds_ra_api make_api() {
    nds_ra_api api{};
    api.ra_rdev_deinit = fake_rdev_deinit;
    api.ra_typical_qp_create = fake_qp_create;
    api.ra_ai_qp_create = fake_ai_qp_create;
    api.ra_set_qp_attr_qos = fake_set_qos;
    api.ra_set_qp_attr_timeout = fake_set_u32;
    api.ra_set_qp_attr_retry_count = fake_set_u32;
    api.ra_qp_destroy = fake_qp_destroy;
    api.ra_get_qp_attr = fake_get_qp_attr;
    api.ra_typical_qp_modify = fake_modify;
    api.ra_rdev_get_port_status = fake_port_status;
    api.ra_get_qp_status = fake_qp_status;
    api.ra_rdev_get_support_lite = fake_lite_support;
    api.ra_rdev_get_cqe_error_list = fake_cqe_errors;
    api.ra_register_mr = fake_register_mr;
    api.ra_deregister_mr = fake_deregister_mr;
    api.ra_typical_send_wr = fake_send;
    api.ra_recv_wrlist = fake_recv;
    api.ra_poll_cq = fake_poll;
    return api;
}

nds_qp_info peer_info() {
    nds_qp_info peer{};
    peer.qp_num = 0x2000U;
    peer.psn = 0x3000U;
    peer.port_num = 1U;
    peer.path_mtu = 1024U;
    peer.gid_index = 4U;
    peer.retry_count = 7U;
    peer.retry_timeout = 14U;
    return peer;
}

TEST(EndpointTest, CreatesAdvertisesAndConnectsQueuePair) {
    FakeRa fake{};
    fake_state = &fake;
    nds::client::Endpoint endpoint;
    nds::client::EndpointTestAccess::adopt(&endpoint, make_api(), &fake_rdev);
    nds::client::QueuePairConfig config{};
    config.traffic_class = 7U;
    config.service_level = 2U;

    auto created = endpoint.create_qp(config);
    ASSERT_TRUE(created);
    auto qp = std::move(*created);
    const auto local = qp.local_qp_info();
    ASSERT_TRUE(local);
    EXPECT_EQ(local->qp_num, 0x1234U);
    EXPECT_EQ(local->psn, 0x4567U);
    EXPECT_EQ(local->gid[15], 42U);
    EXPECT_TRUE(qp.connect(peer_info()));
    EXPECT_TRUE(qp.connected());
    EXPECT_EQ(fake.modify_calls, 1);
    int status = -1;
    EXPECT_TRUE(qp.query_port_status(&status));
    EXPECT_EQ(status, NDS_RA_PORT_STATUS_ACTIVE);
    EXPECT_TRUE(qp.query_status(&status));
    EXPECT_EQ(status, NDS_RA_QP_STATUS_CONNECTED);
    EXPECT_TRUE(qp.query_support_lite(&status));
    EXPECT_EQ(status, NDS_RA_LITE_ALIGN_4K);
}

TEST(EndpointTest, ProducesAiQueuePairDeviceView) {
    FakeRa fake{};
    fake_state = &fake;
    nds::client::Endpoint endpoint;
    nds::client::EndpointTestAccess::adopt(&endpoint, make_api(), &fake_rdev);
    nds::client::QueuePairConfig config{};
    config.send_queue_depth = 64U;
    config.receive_queue_depth = 32U;

    auto created = endpoint.create_qp(config, nds::client::NpuExecutionMode::Aiv);
    ASSERT_TRUE(created);
    auto qp = std::move(*created);
    EXPECT_EQ(fake.ai_qp_create_calls, 1);
    EXPECT_EQ(fake.ai_attributes.qp_mode, NDS_RA_QP_MODE_OPBASE_EXT);
    ASSERT_TRUE(qp.set_device_wr_id_storage(0x50000U, 0x60000U));
    const auto transport = qp.make_device_transport();
    ASSERT_TRUE(transport);
    EXPECT_EQ(transport->control_qp.provider_qp_address, 0x1000U);
    EXPECT_EQ(transport->control_qp.send_queue.wr_id_address, 0x50000U);
    EXPECT_EQ(transport->control_qp.receive_queue.wr_id_address, 0x60000U);
}

TEST(EndpointTest, MemoryRegionOwnsRegistration) {
    FakeRa fake{};
    fake_state = &fake;
    nds::client::Endpoint endpoint;
    nds::client::EndpointTestAccess::adopt(&endpoint, make_api(), &fake_rdev);
    nds::client::MemoryBuffer buffer;
    nds::client::EndpointTestAccess::make_host_buffer(&buffer, 64U);
    {
        auto registered = endpoint.reg_mr(buffer, nds::client::MemoryAccess::DirectNpu);
        ASSERT_TRUE(registered);
        auto region = std::move(*registered);
        EXPECT_EQ(region.address(), reinterpret_cast<std::uint64_t>(buffer.data()));
        EXPECT_EQ(region.length(), 64U);
        EXPECT_EQ(region.local_key(), 0x1111U);
        EXPECT_EQ(region.remote_key(), 0x2222U);
        EXPECT_TRUE(region.belongs_to(&endpoint));
    }
    EXPECT_EQ(fake.register_calls, 1);
    EXPECT_EQ(fake.deregister_calls, 1);
}

TEST(EndpointTest, RaVerbsUseQueuePairExecutionView) {
    FakeRa fake{};
    fake_state = &fake;
    nds::client::Endpoint endpoint;
    nds::client::EndpointTestAccess::adopt(&endpoint, make_api(), &fake_rdev);
    auto created = endpoint.create_qp({});
    ASSERT_TRUE(created);
    auto qp = std::move(*created);
    ASSERT_TRUE(qp.connect(peer_info()));
    const nds_device_send_wr send{
        1U, NDS_DEVICE_WR_SEND, NDS_DEVICE_SEND_SIGNALED, {0x1000U, 64U, 0x99U}, 0U, 0U, 0U};
    EXPECT_TRUE(nds::NdsRaPostSend(&qp, send));
    EXPECT_EQ(fake.send_calls, 1);
    EXPECT_EQ(fake.send.buffers->address, 0x1000U);
    EXPECT_TRUE(nds::NdsRaPostRecv(&qp, {2U, {0x2000U, 64U, 0x88U}}));
    EXPECT_EQ(fake.recv_calls, 1);
    nds_device_completion_output output{};
    const auto polled = nds::NdsRaPollCq(&qp, NDS_DEVICE_SEND_QUEUE, &output);
    EXPECT_TRUE(polled);
    EXPECT_EQ(*polled, 0U);
}

TEST(EndpointTest, RejectsInvalidQueueDepthAndPeer) {
    FakeRa fake{};
    fake_state = &fake;
    nds::client::Endpoint endpoint;
    nds::client::EndpointTestAccess::adopt(&endpoint, make_api(), &fake_rdev);
    nds::client::QueuePairConfig config{};
    config.send_queue_depth = 3U;
    EXPECT_FALSE(endpoint.create_qp(config));
    config.send_queue_depth = 64U;
    auto created = endpoint.create_qp(config);
    ASSERT_TRUE(created);
    auto qp = std::move(*created);
    EXPECT_FALSE(qp.connect({}));
}

}  // namespace

#include "endpoint.hh"
#include "backends/backend_mode.hh"
#include "ra.hh"
#include "runtime.hh"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace nds::client {

struct EndpointTestAccess {
    static void adopt(Endpoint *endpoint, Libra &&libra, void *rdev) {
        endpoint->runtime_ = reinterpret_cast<Runtime *>(endpoint);
        endpoint->libra_.rdev_deinit = libra.rdev_deinit;
        endpoint->libra_.typical_qp_create = libra.typical_qp_create;
        endpoint->libra_.ai_qp_create = libra.ai_qp_create;
        endpoint->libra_.set_qp_attr_qos = libra.set_qp_attr_qos;
        endpoint->libra_.set_qp_attr_timeout = libra.set_qp_attr_timeout;
        endpoint->libra_.set_qp_attr_retry_count = libra.set_qp_attr_retry_count;
        endpoint->libra_.qp_destroy = libra.qp_destroy;
        endpoint->libra_.get_qp_attr = libra.get_qp_attr;
        endpoint->libra_.typical_qp_modify = libra.typical_qp_modify;
        endpoint->libra_.rdev_get_port_status = libra.rdev_get_port_status;
        endpoint->libra_.get_qp_status = libra.get_qp_status;
        endpoint->libra_.rdev_get_support_lite = libra.rdev_get_support_lite;
        endpoint->libra_.rdev_get_cqe_error_list = libra.rdev_get_cqe_error_list;
        endpoint->libra_.register_mr = libra.register_mr;
        endpoint->libra_.deregister_mr = libra.deregister_mr;
        endpoint->libra_.typical_send_wr = libra.typical_send_wr;
        endpoint->libra_.recv_wrlist = libra.recv_wrlist;
        endpoint->libra_.poll_cq = libra.poll_cq;
        endpoint->rdev_handle_ = rdev;
    }

    static void make_host_buffer(MemoryBuffer *buffer, std::size_t size) {
        buffer->data_ = new std::byte[size];
        buffer->rdma_data_ = buffer->data_;
        buffer->size_ = size;
        buffer->location_ = MemoryLocation::Host;
    }

    static void set_rdma_address(MemoryBuffer *buffer, void *address) {
        buffer->rdma_data_ = address;
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
    int set_device_calls{};
    int doorbell_calls{};
    int recv_calls{};
    int poll_calls{};
    bool last_poll_was_send{};
    int port_status{Libra::PORT_STATUS_ACTIVE};
    int qp_status{Libra::QP_STATUS_CONNECTED};
    int lite_support{Libra::LITE_ALIGN_4K};
    std::uint32_t cqe_error_count{};
    Libra::QpExtAttrs ai_attributes{};
    Libra::TypicalQp local{};
    Libra::TypicalQp remote{};
    Libra::MrInfo mr{};
    Libra::SendWr send{};
    Libra::Sge send_sge{};
    std::uint32_t doorbell_index{};
    std::uint64_t doorbell_info{};
};

FakeRa *fake_state{};
int fake_rdev{};
int fake_qp{};

int fake_rdev_deinit(void *handle, unsigned int notify_type) {
    EXPECT_EQ(handle, &fake_rdev);
    EXPECT_EQ(notify_type, Libra::NOTIFY);
    return 0;
}

int fake_qp_create(void *rdev, int flag, int mode, Libra::TypicalQp *, void **handle) {
    EXPECT_EQ(rdev, &fake_rdev);
    EXPECT_EQ(flag, Libra::QP_FLAG_RC);
    EXPECT_EQ(mode, Libra::QP_MODE_OPBASE);
    ++fake_state->qp_create_calls;
    *handle = &fake_qp;
    return 0;
}

int fake_ai_qp_create(void *rdev, Libra::QpExtAttrs *attributes, Libra::AiQpInfo *info, void **handle) {
    EXPECT_EQ(rdev, &fake_rdev);
    ++fake_state->ai_qp_create_calls;
    fake_state->ai_attributes = *attributes;
    info->ai_qp_address = 0x1000U;
    info->ai_scq_address = 0x2000U;
    info->ai_rcq_address = 0x3000U;
    auto *plane = reinterpret_cast<Libra::AiDataPlaneInfo *>(info->data_plane_info);
    plane->send_wq = {1U, 0U, 0x10000U, 64U, 64U, 0x11000U, 0x12000U, 0x13000U, 0x14000U, {}};
    plane->receive_wq = {2U, 0U, 0x20000U, 16U, 32U, 0x21000U, 0x22000U, 0x23000U, 0x24000U, {}};
    plane->send_cq = {3U, 0U, 0x30000U, 64U, 64U, 0U, 0x32000U, 0x33000U, 0x34000U, {}};
    plane->receive_cq = {4U, 0U, 0x40000U, 64U, 32U, 0U, 0x42000U, 0x43000U, 0x44000U, {}};
    *handle = &fake_qp;
    return 0;
}

int fake_set_qos(void *, Libra::QosAttr *) {
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

int fake_get_qp_attr(void *handle, Libra::QpAttr *attributes) {
    EXPECT_EQ(handle, &fake_qp);
    attributes->qpn = 0x1234U;
    attributes->psn = 0x4567U;
    attributes->gid_index = 3U;
    attributes->gid[15] = 42U;
    return 0;
}

int fake_modify(void *handle, Libra::TypicalQp *local, Libra::TypicalQp *remote) {
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

int fake_cqe_errors(void *, Libra::CqeError *errors, unsigned int *count) {
    if (*count < fake_state->cqe_error_count)
        return -1;
    for (std::uint32_t index = 0U; index < fake_state->cqe_error_count; ++index) {
        errors[index].status = index + 1U;
        errors[index].qp_number = 0x1000U + index;
    }
    *count = fake_state->cqe_error_count;
    return 0;
}

int fake_register_mr(const void *rdev, Libra::MrInfo *info, void **handle) {
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

int fake_send(void *, Libra::SendWr *request, Libra::SendResponse *response) {
    ++fake_state->send_calls;
    fake_state->send = *request;
    fake_state->send_sge = *request->buffers;
    fake_state->send.buffers = &fake_state->send_sge;
    response->doorbell.db_index = 17U;
    response->doorbell.db_info = 0x100000017U;
    return 0;
}

int fake_set_device(std::int32_t logical_device_id) {
    EXPECT_EQ(logical_device_id, 0);
    ++fake_state->set_device_calls;
    return 0;
}

int fake_doorbell(std::uint32_t index, std::uint64_t info, void *stream) {
    EXPECT_EQ(stream, nullptr);
    ++fake_state->doorbell_calls;
    fake_state->doorbell_index = index;
    fake_state->doorbell_info = info;
    return 0;
}

int fake_recv(void *, Libra::RecvWr *, unsigned int count, unsigned int *completed) {
    ++fake_state->recv_calls;
    *completed = count;
    return 0;
}

int fake_poll(void *, bool is_send_cq, unsigned int, void *) {
    ++fake_state->poll_calls;
    fake_state->last_poll_was_send = is_send_cq;
    return 0;
}

Libra make_api() {
    Libra api{};
    api.rdev_deinit = fake_rdev_deinit;
    api.typical_qp_create = fake_qp_create;
    api.ai_qp_create = fake_ai_qp_create;
    api.set_qp_attr_qos = fake_set_qos;
    api.set_qp_attr_timeout = fake_set_u32;
    api.set_qp_attr_retry_count = fake_set_u32;
    api.qp_destroy = fake_qp_destroy;
    api.get_qp_attr = fake_get_qp_attr;
    api.typical_qp_modify = fake_modify;
    api.rdev_get_port_status = fake_port_status;
    api.get_qp_status = fake_qp_status;
    api.rdev_get_support_lite = fake_lite_support;
    api.rdev_get_cqe_error_list = fake_cqe_errors;
    api.register_mr = fake_register_mr;
    api.deregister_mr = fake_deregister_mr;
    api.typical_send_wr = fake_send;
    api.recv_wrlist = fake_recv;
    api.poll_cq = fake_poll;
    return api;
}

nds::QpInfo peer_info() {
    nds::QpInfo peer{};
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

    auto created = endpoint.create_qp(config, nds::client::BackendMode::Ra);
    ASSERT_TRUE(created.ok());
    auto qp = std::move(created).value();
    const auto local = qp.local_qp_info();
    ASSERT_TRUE(local.ok());
    EXPECT_EQ(local.value().qp_num, 0x1234U);
    EXPECT_EQ(local.value().psn, 0x4567U);
    EXPECT_EQ(local.value().gid[15], 42U);
    EXPECT_TRUE(qp.connect(peer_info()).ok());
    EXPECT_TRUE(qp.connected());
    EXPECT_EQ(fake.modify_calls, 1);
    const auto port_status = qp.query_port_status();
    ASSERT_TRUE(port_status.ok());
    EXPECT_EQ(port_status.value(), Libra::PORT_STATUS_ACTIVE);
    const auto qp_status = qp.query_status();
    ASSERT_TRUE(qp_status.ok());
    EXPECT_EQ(qp_status.value(), Libra::QP_STATUS_CONNECTED);
    const auto lite_support = qp.query_support_lite();
    ASSERT_TRUE(lite_support.ok());
    EXPECT_EQ(lite_support.value(), Libra::LITE_ALIGN_4K);
    fake.cqe_error_count = 2U;
    const auto cqe_errors = qp.query_cqe_errors();
    ASSERT_TRUE(cqe_errors.ok());
    ASSERT_EQ(cqe_errors.value().size(), 2U);
    EXPECT_EQ(cqe_errors.value()[1].qp_number, 0x1001U);
}

TEST(EndpointTest, MemoryRegionOwnsRegistration) {
    FakeRa fake{};
    fake_state = &fake;
    nds::client::Endpoint endpoint;
    nds::client::EndpointTestAccess::adopt(&endpoint, make_api(), &fake_rdev);
    nds::client::MemoryBuffer buffer;
    nds::client::EndpointTestAccess::make_host_buffer(&buffer, 64U);
    {
        auto registered =
            endpoint.reg_mr(buffer, nds::client::MemoryAccess::LocalWrite | nds::client::MemoryAccess::RemoteWrite |
                                        nds::client::MemoryAccess::RemoteRead);
        ASSERT_TRUE(registered.ok());
        auto region = std::move(registered).value();
        EXPECT_EQ(region.address(), reinterpret_cast<std::uint64_t>(buffer.data()));
        EXPECT_EQ(region.length(), 64U);
        EXPECT_EQ(region.local_key(), 0x1111U);
        EXPECT_EQ(region.remote_key(), 0x2222U);
        EXPECT_TRUE(region.belongs_to(&endpoint));
    }
    EXPECT_EQ(fake.register_calls, 1);
    EXPECT_EQ(fake.deregister_calls, 1);
}

TEST(EndpointTest, MemoryRegionRegistersMappedHostAddress) {
    FakeRa fake{};
    fake_state = &fake;
    nds::client::Endpoint endpoint;
    nds::client::EndpointTestAccess::adopt(&endpoint, make_api(), &fake_rdev);
    nds::client::MemoryBuffer buffer;
    nds::client::EndpointTestAccess::make_host_buffer(&buffer, 64U);
    void *const mapped_address = reinterpret_cast<void *>(static_cast<std::uintptr_t>(0x10000U));
    nds::client::EndpointTestAccess::set_rdma_address(&buffer, mapped_address);

    auto registered =
        endpoint.reg_mr(buffer, nds::client::MemoryAccess::LocalWrite | nds::client::MemoryAccess::RemoteWrite |
                                    nds::client::MemoryAccess::RemoteRead);
    ASSERT_TRUE(registered.ok());
    EXPECT_EQ(fake.mr.address, mapped_address);
    EXPECT_EQ(registered.value().address(), reinterpret_cast<std::uint64_t>(mapped_address));
}

TEST(EndpointTest, RaVerbsUseQueuePairExecutionView) {
    FakeRa fake{};
    fake_state = &fake;
    nds::client::Endpoint endpoint;
    nds::client::EndpointTestAccess::adopt(&endpoint, make_api(), &fake_rdev);
    auto created = endpoint.create_qp({}, nds::client::BackendMode::Ra);
    ASSERT_TRUE(created.ok());
    auto qp = std::move(created).value();
    ASSERT_TRUE(qp.connect(peer_info()).ok());
    nds::client::Runtime runtime;
    runtime.libruntime().set_device = fake_set_device;
    runtime.libruntime().rdma_db_send = fake_doorbell;
    const NdsSendWr send{
        .wr_id = 1U,
        .opcode = NDS_WR_SEND,
        .flags = NDS_SEND_SIGNALED,
        .local = {.address = 0x1000U, .length = 64U, .local_key = 0x99U},
        .remote_address = 0U,
        .remote_key = 0U,
    };
    EXPECT_TRUE(nds::NdsRaPostSend(&runtime, &qp, send, nullptr).ok());
    EXPECT_EQ(fake.send_calls, 1);
    EXPECT_EQ(fake.send.buffers->address, 0x1000U);
    EXPECT_EQ(fake.set_device_calls, 1);
    EXPECT_EQ(fake.doorbell_calls, 1);
    EXPECT_EQ(fake.doorbell_index, 17U);
    EXPECT_EQ(fake.doorbell_info, 0x100000017U);
    EXPECT_TRUE(nds::NdsRaPostRecv(&qp, {2U, {0x2000U, 64U, 0x88U}}).ok());
    EXPECT_EQ(fake.recv_calls, 1);
    NdsWc output[NDS_MAX_COMPLETIONS]{};
    const auto polled = nds::NdsRaPollCq(&qp, true, 1U, output);
    EXPECT_TRUE(polled.ok());
    EXPECT_EQ(polled.value(), 0U);
    EXPECT_TRUE(fake.last_poll_was_send);
    const auto receive_polled = nds::NdsRaPollCq(&qp, false, 1U, output);
    EXPECT_TRUE(receive_polled.ok());
    EXPECT_EQ(receive_polled.value(), 0U);
    EXPECT_FALSE(fake.last_poll_was_send);
}

TEST(EndpointTest, RejectsInvalidQueueDepthAndPeer) {
    FakeRa fake{};
    fake_state = &fake;
    nds::client::Endpoint endpoint;
    nds::client::EndpointTestAccess::adopt(&endpoint, make_api(), &fake_rdev);
    nds::client::QueuePairConfig config{};
    config.send_queue_depth = 3U;
    EXPECT_FALSE(endpoint.create_qp(config, nds::client::BackendMode::Ra).ok());
    config.send_queue_depth = 64U;
    auto created = endpoint.create_qp(config, nds::client::BackendMode::Ra);
    ASSERT_TRUE(created.ok());
    auto qp = std::move(created).value();
    EXPECT_FALSE(qp.connect({}).ok());
}

}  // namespace

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
    int modify_calls{};
    nds_ra_rdev rdev{};
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

int fake_modify(void *handle, nds_ra_typical_qp *local, nds_ra_typical_qp *remote)
{
    assert(handle == &fake_qp);
    ++state->modify_calls;
    state->local = *local;
    state->remote = *remote;
    return 0;
}

nds_ra_api make_fake_api()
{
    nds_ra_api api{};
    api.ra_rdev_init = fake_rdev_init;
    api.ra_rdev_deinit = fake_rdev_deinit;
    api.ra_typical_qp_create = fake_qp_create;
    api.ra_qp_destroy = fake_qp_destroy;
    api.ra_get_qp_attr = fake_get_attributes;
    api.ra_typical_qp_modify = fake_modify;
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
    assert(fake.rdev_init_calls == 1);
    assert(fake.rdev_mode == NDS_RA_NETWORK_OFFLINE);
    assert(fake.notify_type == NDS_RA_NOTIFY);
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
    assert(qp.make_qp_only_endpoint(endpoint));
    assert(!qp.make_data_ready_endpoint(0, 0, endpoint));
    assert(!qp.connect(invalid_peer));
    qp.reset();
}

} // namespace

int main()
{
    test_create_advertise_connect_and_reset();
    test_rejects_invalid_configuration_and_endpoint();
    return 0;
}

#include "loaders/cann_runtime_loader.hh"

#include <cstring>

#include <gtest/gtest.h>

TEST(CannRuntimeLoaderIntegrationTest, ResolvesAndDispatchesDoorbellAbi) {
    NdsRtProcExtParam parameter{"--hdcType=18", 12U};
    NdsRtNetServiceOpenArgs args{&parameter, 1U};

    EXPECT_FALSE(nds_cann_runtime_open(""));
    auto api = nds_cann_runtime_open(NDS_FAKE_CANN_RUNTIME_PATH);
    ASSERT_TRUE(api) << api.error().message;
    ASSERT_NE(api->rdma_db_send, nullptr);
    ASSERT_NE(api->set_device, nullptr);
    ASSERT_NE(api->open_net_service, nullptr);
    ASSERT_NE(api->close_net_service, nullptr);
    EXPECT_EQ(api->set_device(0), 0);
    EXPECT_EQ(api->open_net_service(&args), 0);
    EXPECT_EQ(api->rdma_db_send(0x1234U, UINT64_C(0x10000006a), nullptr), 0);
    EXPECT_EQ(api->close_net_service(), 0);
    EXPECT_EQ(api->rdma_db_send(0x1234U, UINT64_C(0xdead), nullptr), -77);
    nds_cann_runtime_close(&*api);
    EXPECT_EQ(api->library, nullptr);
    EXPECT_EQ(api->rdma_db_send, nullptr);
}

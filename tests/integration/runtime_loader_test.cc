#include "nds/runtime_loader.h"

#include <cstring>

#include <gtest/gtest.h>

TEST(RuntimeLoaderIntegrationTest, ResolvesAndDispatchesDoorbellAbi) {
    NdsRuntimeApi api{};
    NdsRtProcExtParam parameter{"--hdcType=18", 12U};
    NdsRtNetServiceOpenArgs args{&parameter, 1U};

    EXPECT_NE(nds_runtime_open(&api, ""), 0);
    EXPECT_NE(std::strstr(nds_runtime_error(&api), "non-empty library path"), nullptr);
    ASSERT_EQ(nds_runtime_open(&api, NDS_FAKE_RUNTIME_PATH), 0) << nds_runtime_error(&api);
    ASSERT_NE(api.rdma_db_send, nullptr);
    ASSERT_NE(api.set_device, nullptr);
    ASSERT_NE(api.open_net_service, nullptr);
    ASSERT_NE(api.close_net_service, nullptr);
    EXPECT_EQ(api.set_device(0), 0);
    EXPECT_EQ(api.open_net_service(&args), 0);
    EXPECT_EQ(api.rdma_db_send(0x1234U, UINT64_C(0x10000006a), nullptr), 0);
    EXPECT_EQ(api.close_net_service(), 0);
    EXPECT_EQ(api.rdma_db_send(0x1234U, UINT64_C(0xdead), nullptr), -77);
    nds_runtime_close(&api);
    EXPECT_EQ(api.library, nullptr);
    EXPECT_EQ(api.rdma_db_send, nullptr);
}

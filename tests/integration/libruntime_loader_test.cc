#include "loaders/libruntime.hh"

#include <cstring>

#include <gtest/gtest.h>

TEST(CannRuntimeLoaderIntegrationTest, ResolvesAndDispatchesDoorbellAbi) {
    Libruntime::ProcExtParam parameter{"--hdcType=18", 12U};
    Libruntime::NetServiceOpenArgs args{&parameter, 1U};

    nds::Result<Libruntime> runtime_result = Libruntime::open(NDS_FAKE_LIBRUNTIME_PATH);
    ASSERT_TRUE(runtime_result.ok()) << runtime_result.error().message;
    const Libruntime &runtime = runtime_result.value();
    EXPECT_EQ(runtime.set_device(0), 0);
    EXPECT_EQ(runtime.open_net_service(&args), 0);
    EXPECT_EQ(runtime.rdma_db_send(0x1234U, UINT64_C(0x10000006a), nullptr), 0);
    EXPECT_EQ(runtime.close_net_service(), 0);
    EXPECT_EQ(runtime.rdma_db_send(0x1234U, UINT64_C(0xdead), nullptr), -77);
}

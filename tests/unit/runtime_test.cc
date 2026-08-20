#include "runtime.hh"

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <cstdint>

namespace nds::client {

int *pinned_allocations{};
int *pinned_frees{};
std::uint64_t registered_size{};
std::uint32_t registered_flags{};
void *registered_device_pointer = reinterpret_cast<void *>(static_cast<std::uintptr_t>(0x2000U));

int fake_host_register(void *host_ptr, std::uint64_t size, int type, void **device_ptr) {
    if (pinned_allocations != nullptr)
        ++*pinned_allocations;
    registered_size = size;
    registered_flags = static_cast<std::uint32_t>(type);
    (void)host_ptr;
    *device_ptr = registered_device_pointer;
    return 0;
}

int fake_host_unregister(void *) {
    if (pinned_frees != nullptr)
        ++*pinned_frees;
    return 0;
}

struct RuntimeTestAccess {
    static void adopt_pinned_allocator(Runtime *runtime, int *allocations, int *frees) {
        runtime->initialized_ = true;
        pinned_allocations = allocations;
        pinned_frees = frees;
        registered_size = 0U;
        registered_flags = 0U;
        registered_device_pointer = reinterpret_cast<void *>(static_cast<std::uintptr_t>(0x2000U));
        runtime->acl_.host_register = fake_host_register;
        runtime->acl_.host_unregister = fake_host_unregister;
    }
};

}  // namespace nds::client

namespace {

TEST(RuntimeTest, HostPinnedBufferUsesPinnedAllocatorAndCopiesInPlace) {
    nds::client::Runtime runtime;
    int allocations = 0;
    int frees = 0;
    nds::client::RuntimeTestAccess::adopt_pinned_allocator(&runtime, &allocations, &frees);

    const char source[] = "pinned storage";
    char destination[sizeof(source)]{};
    {
        auto allocated = runtime.allocate(sizeof(source), nds::client::MemoryLocation::HostPinned);
        ASSERT_TRUE(allocated);
        auto buffer = std::move(*allocated);
        EXPECT_EQ(buffer.location(), nds::client::MemoryLocation::HostPinned);
        EXPECT_EQ(reinterpret_cast<std::uintptr_t>(buffer.data()) % 4096U, 0U);
        EXPECT_EQ(buffer.rdma_data(), nds::client::registered_device_pointer);
        EXPECT_NE(buffer.rdma_data(), buffer.data());
        EXPECT_EQ(allocations, 1);
        EXPECT_EQ(nds::client::registered_size, 4096U);
        EXPECT_EQ(nds::client::registered_flags, 0U);
        EXPECT_TRUE(runtime.copy_to(&buffer, source, sizeof(source)));
        EXPECT_TRUE(runtime.copy_from(destination, buffer, sizeof(destination)));
        EXPECT_EQ(std::memcmp(destination, source, sizeof(source)), 0);
    }
    EXPECT_EQ(frees, 1);
}

TEST(RuntimeTest, HostPinnedAllocationReportsUnsupportedAllocator) {
    nds::client::Runtime runtime;
    nds::client::RuntimeTestAccess::adopt_pinned_allocator(&runtime, nullptr, nullptr);
    runtime.acl_api().host_register = nullptr;
    runtime.acl_api().host_unregister = nullptr;

    const auto allocated = runtime.allocate(64U, nds::client::MemoryLocation::HostPinned);
    ASSERT_FALSE(allocated);
    EXPECT_EQ(allocated.error().code, nds::ErrorCode::kUnsupported);
}

}  // namespace

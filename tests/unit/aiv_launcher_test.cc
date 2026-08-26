#include "aiv/host/launcher.hh"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {

struct FakeState {
    bool binary_loaded{};
    bool binary_unloaded{};
    bool stream_created{};
    bool stream_destroyed{};
    bool batch_function_resolved{};
    bool batch_launched{};
    bool synchronized{};
};

FakeState state;
int binary_handle;
int function_handle;
int stream_handle;

int load_binary(const char *path, NdsAclBinaryLoadOptions *options, NdsAclBinHandle *handle) {
    EXPECT_STREQ(path, "/tmp/nds_aiv_kernel.o");
    EXPECT_NE(options, nullptr);
    EXPECT_EQ(options->num_options, 1U);
    EXPECT_EQ(options->options[0].type, NDS_ACL_BINARY_LOAD_OPT_LAZY_LOAD);
    EXPECT_EQ(options->options[0].value.lazy_load, 1U);
    EXPECT_NE(handle, nullptr);
    state.binary_loaded = true;
    *handle = &binary_handle;
    return 0;
}

int unload_binary(NdsAclBinHandle handle) {
    EXPECT_EQ(handle, &binary_handle);
    state.binary_unloaded = true;
    return 0;
}

int get_function(NdsAclBinHandle handle, const char *name, NdsAclFuncHandle *function) {
    EXPECT_EQ(handle, &binary_handle);
    EXPECT_STREQ(name, "NdsAivPostSendBatch");
    EXPECT_NE(function, nullptr);
    state.batch_function_resolved = true;
    *function = &function_handle;
    return 0;
}

int launch(NdsAclFuncHandle function, std::uint32_t block_count, NdsAclStream stream, NdsAclLaunchKernelConfig *config,
           void *host_args, std::size_t args_size, void *placeholder_array, std::size_t placeholder_count) {
    EXPECT_EQ(function, &function_handle);
    EXPECT_EQ(block_count, 1U);
    EXPECT_EQ(stream, &stream_handle);
    EXPECT_NE(config, nullptr);
    EXPECT_EQ(config->num_attrs, 2U);
    EXPECT_EQ(config->attrs[0].id, NDS_ACL_LAUNCH_KERNEL_ATTR_SCHEM_MODE);
    EXPECT_EQ(config->attrs[1].id, NDS_ACL_LAUNCH_KERNEL_ATTR_ENGINE_TYPE);
    EXPECT_NE(host_args, nullptr);
    EXPECT_EQ(args_size, sizeof(std::uint64_t));
    EXPECT_EQ(placeholder_array, nullptr);
    EXPECT_EQ(placeholder_count, 0U);
    std::uint64_t args_address{};
    std::memcpy(&args_address, host_args, sizeof(args_address));
    EXPECT_EQ(args_address, UINT64_C(0x1000));
    state.batch_launched = true;
    return 0;
}

int create_stream(NdsAclStream *stream) {
    EXPECT_NE(stream, nullptr);
    state.stream_created = true;
    *stream = &stream_handle;
    return 0;
}

int destroy_stream(NdsAclStream stream) {
    EXPECT_EQ(stream, &stream_handle);
    state.stream_destroyed = true;
    return 0;
}

int synchronize(NdsAclStream stream, std::int32_t timeout_ms) {
    EXPECT_EQ(stream, &stream_handle);
    EXPECT_EQ(timeout_ms, 5000);
    state.synchronized = true;
    return 0;
}

}  // namespace

TEST(AivLauncherTest, LaunchesBatchPostSendWithOneDeviceAddressArgument) {
    state = {};
    NdsAclApi api{};
    api.binary_load_from_file = load_binary;
    api.binary_unload = unload_binary;
    api.binary_get_function = get_function;
    api.launch_kernel_with_host_args = launch;
    api.create_stream = create_stream;
    api.destroy_stream = destroy_stream;
    api.synchronize_stream_with_timeout = synchronize;

    {
        nds::AivEntrypointLauncher launcher;
        ASSERT_TRUE(launcher.load(&api, "/tmp/nds_aiv_kernel.o"));
        EXPECT_TRUE(launcher.launch_post_send_batch_and_wait(UINT64_C(0x1000), 5000));
        EXPECT_TRUE(state.batch_function_resolved && state.batch_launched && state.synchronized);
    }
    EXPECT_TRUE(state.binary_loaded && state.stream_created && state.stream_destroyed && state.binary_unloaded);
}

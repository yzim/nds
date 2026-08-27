#include "aicpu/host/launcher.hh"

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
    std::uint32_t function_resolve_count{};
    bool launched{};
    bool synchronized{};
    std::uint64_t expected_args_address{};
};

FakeState state;
int binary_handle;
int function_handle;
int stream_handle;

int load_binary(const char *path, NdsAclBinaryLoadOptions *options, NdsAclBinHandle *handle) {
    EXPECT_TRUE(path != nullptr && std::strcmp(path, "/tmp/nds_aicpu_standard.json") == 0);
    EXPECT_TRUE(options != nullptr && options->num_options == 1U && options->options != nullptr);
    EXPECT_TRUE(options->options[0].type == NDS_ACL_BINARY_LOAD_OPT_CPU_KERNEL_MODE);
    EXPECT_TRUE(options->options[0].value.cpu_kernel_mode == NDS_ACL_CPU_KERNEL_REGISTER_JSON);
    EXPECT_TRUE(handle != nullptr);
    state.binary_loaded = true;
    *handle = &binary_handle;
    return 0;
}

int unload_binary(NdsAclBinHandle handle) {
    EXPECT_TRUE(handle == &binary_handle);
    state.binary_unloaded = true;
    return 0;
}

int get_function(NdsAclBinHandle handle, const char *name, NdsAclFuncHandle *function) {
    EXPECT_EQ(handle, &binary_handle);
    EXPECT_STREQ(name, "nds_aicpu_post_send_kernel");
    EXPECT_NE(function, nullptr);
    ++state.function_resolve_count;
    *function = &function_handle;
    return 0;
}
int launch(NdsAclFuncHandle function, std::uint32_t block_count, NdsAclStream stream, NdsAclLaunchKernelConfig *config,
           void *host_args, std::size_t args_size, void *placeholder_array, std::size_t placeholder_count) {
    EXPECT_EQ(function, &function_handle);
    EXPECT_EQ(block_count, 1U);
    EXPECT_EQ(stream, &stream_handle);
    EXPECT_NE(config, nullptr);
    EXPECT_EQ(config->num_attrs, 1U);
    EXPECT_EQ(config->attrs[0].id, NDS_ACL_LAUNCH_KERNEL_ATTR_TIMEOUT);
    EXPECT_EQ(config->attrs[0].value.timeout_seconds, 5U);
    EXPECT_NE(host_args, nullptr);
    if (host_args != nullptr) {
        EXPECT_EQ(*static_cast<const std::uint64_t *>(host_args), state.expected_args_address);
    }
    EXPECT_EQ(args_size, sizeof(state.expected_args_address));
    EXPECT_EQ(placeholder_array, nullptr);
    EXPECT_EQ(placeholder_count, 0U);
    state.launched = true;
    return 0;
}

int create_stream(NdsAclStream *stream, std::uint32_t, std::uint32_t) {
    EXPECT_TRUE(stream != nullptr);
    state.stream_created = true;
    *stream = &stream_handle;
    return 0;
}

int destroy_stream(NdsAclStream stream) {
    EXPECT_TRUE(stream == &stream_handle);
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

TEST(AicpuLauncherTest, LaunchesPostSendWithTheDeviceEnvelopeAddress) {
    state = {};
    NdsAclApi api{};
    api.binary_load_from_file = load_binary;
    api.binary_unload = unload_binary;
    api.binary_get_function = get_function;
    api.launch_kernel_with_host_args = launch;
    api.create_stream_with_config = create_stream;
    api.destroy_stream = destroy_stream;
    api.synchronize_stream_with_timeout = synchronize;

    {
        nds::AicpuLauncher launcher;
        EXPECT_TRUE(launcher.load(&api, "/tmp/nds_aicpu_standard.json"));
        EXPECT_TRUE(launcher.loaded());
        EXPECT_TRUE(state.binary_loaded && state.stream_created);
        state.expected_args_address = UINT64_C(0x1000);
        EXPECT_TRUE(launcher.launch_and_wait("nds_aicpu_post_send_kernel", state.expected_args_address, 5000));
        EXPECT_TRUE(launcher.launch_and_wait("nds_aicpu_post_send_kernel", state.expected_args_address, 5000));
        EXPECT_EQ(state.function_resolve_count, 1U);
        EXPECT_TRUE(state.launched && state.synchronized);
    }
    EXPECT_TRUE(state.stream_destroyed && state.binary_unloaded);
}

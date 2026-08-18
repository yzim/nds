#include "aicpu/host/launcher.hh"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {

struct FakeState {
    bool binary_loaded{};
    bool binary_unloaded{};
    bool stream_created{};
    bool stream_destroyed{};
};

FakeState state;
int binary_handle;
int stream_handle;

int load_binary(const char *path, nds_acl_binary_load_options *options, nds_acl_bin_handle *handle) {
    assert(path != nullptr && std::strcmp(path, "/tmp/libnds_aicpu_standard.so") == 0);
    assert(options != nullptr && options->num_options == 1U && options->options != nullptr);
    assert(options->options[0].type == NDS_ACL_BINARY_LOAD_OPT_CPU_KERNEL_MODE);
    assert(options->options[0].value.cpu_kernel_mode == NDS_ACL_CPU_KERNEL_LOAD_SO_AND_JSON);
    assert(handle != nullptr);
    state.binary_loaded = true;
    *handle = &binary_handle;
    return 0;
}

int unload_binary(nds_acl_bin_handle handle) {
    assert(handle == &binary_handle);
    state.binary_unloaded = true;
    return 0;
}

int get_function(nds_acl_bin_handle, const char *, nds_acl_func_handle *) {
    return 0;
}
int init_args(nds_acl_func_handle, nds_acl_args_handle *) {
    return 0;
}
int append_arg(nds_acl_args_handle, void *, std::size_t, nds_acl_param_handle *) {
    return 0;
}
int finalize_args(nds_acl_args_handle) {
    return 0;
}
int launch(nds_acl_func_handle, std::uint32_t, nds_acl_stream, nds_acl_launch_kernel_config *, nds_acl_args_handle,
           void *) {
    return 0;
}

int create_stream(nds_acl_stream *stream, std::uint32_t, std::uint32_t) {
    assert(stream != nullptr);
    state.stream_created = true;
    *stream = &stream_handle;
    return 0;
}

int destroy_stream(nds_acl_stream stream) {
    assert(stream == &stream_handle);
    state.stream_destroyed = true;
    return 0;
}

int synchronize(nds_acl_stream, std::int32_t) {
    return 0;
}

}  // namespace

int main() {
    nds_acl_api api{};
    api.binary_load_from_file = load_binary;
    api.binary_unload = unload_binary;
    api.binary_get_function = get_function;
    api.kernel_args_init = init_args;
    api.kernel_args_append = append_arg;
    api.kernel_args_finalize = finalize_args;
    api.launch_kernel_with_config = launch;
    api.create_stream_with_config = create_stream;
    api.destroy_stream = destroy_stream;
    api.synchronize_stream_with_timeout = synchronize;

    {
        nds::AicpuEntrypointLauncher launcher;
        assert(launcher.load(&api, "/tmp/libnds_aicpu_standard.so"));
        assert(launcher.loaded());
        assert(state.binary_loaded && state.stream_created);
    }
    assert(state.stream_destroyed && state.binary_unloaded);
    return 0;
}

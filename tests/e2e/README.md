# Hardware end-to-end tests

These tests run only when CMake is configured with
`NDS_ENABLE_E2E_TESTS=ON`. They require one Ascend NPU RNIC, one CPU RoCE
RNIC, passwordless `sudo -n`, and a target build containing the selected
device backends.

The tracked runner contains no topology or installation values. Set these
variables in the target's ignored operational environment:

```text
NDS_E2E_BUILD_DIR   absolute target build directory
NDS_E2E_CANN_ROOT   absolute CANN installation root
NDS_E2E_SOURCE_DIR  absolute NDS source directory
NDS_E2E_NPU_IP      NPU RNIC IPv4 address
NDS_E2E_CPU_IP      CPU RNIC IPv4 address
NDS_E2E_TCP_PORT    unused TCP bootstrap port
NDS_E2E_DEVICE      CPU verbs device name
NDS_E2E_GID_INDEX   CPU verbs GID index
NDS_E2E_STATE_DIR   optional writable log and temporary-state directory
NDS_E2E_TORCH_PYTHON Python interpreter with compatible torch and torch_npu
```

Configure and inspect registration without touching hardware:

```sh
cmake -S . -B build-e2e \
  -DNDS_ENABLE_E2E_TESTS=ON \
  -DNDS_BUILD_AIV_KERNEL=ON \
  -DNDS_BUILD_AICPU_KERNEL=ON \
  -DNDS_CANN_ROOT=<cann-root>
ctest --test-dir build-e2e -N --label-regex '^e2e$'
```

To register the RA Torch session case, also configure the optional wrapper with
`-DNDS_BUILD_TORCH_WRAPPERS=ON`, a compatible `CMAKE_PREFIX_PATH`, and
`-DPython3_EXECUTABLE=<torch-python>`. Set `NDS_E2E_TORCH_PYTHON` to that
interpreter when running the test. Configuring the AIV and AICPU kernel targets
also registers `e2e.aiv_torch_storage_session` and
`e2e.aicpu_torch_storage_session`.

Run one bounded, payload-verified case on the approved target. The runner
accepts the backend (`ra`, `aiv`, or `aicpu`) and operation (`read` or `write`)
explicitly:

```sh
env <NDS_E2E_* assignments> \
  tests/e2e/run_storage.sh --backend aicpu --operation read
```

Use `--sweep` to run all six backend/operation combinations sequentially; the
sweep stops at the first failed case. CTest registers the same six cases and
serializes them with the `ascend_npu0` resource lock:

```sh
env <NDS_E2E_* assignments> \
ctest --test-dir build-e2e --output-on-failure --label-regex '^e2e$'
```

Each case transfers 4096 bytes. Read starts the server with its deterministic
seed pattern, which the client verifies. Write enables server-side payload
verification. The AICPU case creates a private mount namespace for its
standard-CP1 package overlay; it does not modify the installed CANN files.

When `NDS_BUILD_HARDWARE_PROBES=ON` is configured, CTest also registers the
paired verbs and transport example cases for each available backend. These
cases launch the matching example client and server and require both programs
to complete one Send/receive exchange.

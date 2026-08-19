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
NDS_E2E_NPU_IP      NPU RNIC IPv4 address
NDS_E2E_CPU_IP      CPU RNIC IPv4 address
NDS_E2E_TCP_PORT    unused TCP bootstrap port
NDS_E2E_DEVICE      CPU verbs device name
NDS_E2E_GID_INDEX   CPU verbs GID index
NDS_E2E_STATE_DIR   optional writable log and temporary-state directory
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

Run one bounded case on the approved target, supplying the variables through
the target's ignored environment:

```sh
sudo -n env <NDS_E2E_* assignments> \
  ctest --test-dir build-e2e --output-on-failure \
    --tests-regex '^e2e\.aicpu_storage$'
```

Each case starts one server, executes one 4096-byte payload-verified storage
Write, and stops. CTest serializes all cases with the `ascend_npu0` resource
lock. The AICPU case creates a private mount namespace for its standard-CP1
package overlay; it does not modify the installed CANN files.

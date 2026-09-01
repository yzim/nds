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
NDS_E2E_SERVER_ADDRESS server TCP exchange address as IPv4:port
NDS_E2E_DEVICE      CPU verbs device name
NDS_E2E_GID_INDEX   CPU verbs GID index
NDS_E2E_STATE_DIR   optional writable log and temporary-state directory
NDS_E2E_TORCH_PYTHON Python interpreter with compatible torch and torch_npu
```

The E2E tree follows the public library layers:

```text
tests/e2e/
  verbs/       one-QP Send, Recv, Read, PollCq, and configured RDMA Write case
  transport/   RdmaSend, RdmaRecv, RdmaRead, and RdmaWrite cases
  storage/     native storage and Torch session cases
  support/     shared target runners and AICPU package setup
```

Each case runs the matching program in `examples/<layer>/`; examples are the
runnable layer workflows and E2E owns target orchestration and registration.
Each layer owns its runner (`verbs/run.sh`, `transport/run.sh`, or
`storage/run.sh`). `support/common.sh` provides only target setup, client
backend launch, per-case state, and cleanup; it does not select a layer or
operation.

Configure and inspect registration without touching hardware:

```sh
source <cann-root>/set_env.sh
cmake -S . -B build-e2e \
  -DNDS_ENABLE_E2E_TESTS=ON \
  -DNDS_BUILD_AIV_KERNEL=ON \
  -DNDS_BUILD_AICPU_KERNEL=ON
ctest --test-dir build-e2e -N --label-regex '^e2e$'
```

To register storage Torch session cases, also configure the optional wrapper with
`-DNDS_BUILD_TORCH_WRAPPERS=ON`, a compatible `CMAKE_PREFIX_PATH`, and
`-DPython3_EXECUTABLE=<torch-python>`. Set `NDS_E2E_TORCH_PYTHON` to that
interpreter when running a session case.

Run one bounded, payload-verified case on the approved target. The runner
accepts the backend (`ra`, `aiv`, or `aicpu`) and operation (`read`, `write`,
`batch-read`, or `batch-write`) explicitly:

```sh
env <NDS_E2E_* assignments> \
  tests/e2e/storage/run.sh --backend-mode aicpu --operation read
```

Use `--sweep` to run all backend/operation combinations sequentially; the
sweep stops at the first failed case. CTest registers the same cases and
serializes them with the `ascend_npu0` resource lock:

```sh
env <NDS_E2E_* assignments> \
ctest --test-dir build-e2e --output-on-failure --label-regex '^e2e$'
```

Each single case transfers 4096 bytes; each batch case transfers two 4096-byte
entries in one command. Read starts the server with its deterministic seed
pattern, which the client verifies. Write enables server-side payload
verification. AICPU uses the CANN-root mode-0 package in the client executable.
Install that package explicitly before running the test; the E2E runner itself
does not modify the CANN installation.

When `NDS_BUILD_HARDWARE_PROBES=ON` is configured, CTest registers one direct
verbs case for each available RA, AIV, and AICPU backend. Each case launches the
matching example client and server and exercises the complete lower-level
workflow: the client posts Send, posts Recv, performs an RDMA Read, polls its
caller-owned CQ, and performs a configured stream-based RDMA Write. The server
verifies the received and written payload. For AIV and AICPU, the client requests caller-owned CQ
metadata when creating the AI-QP; AICPU still uses the installed CANN-root
mode-0 package for its provider-backed Send path.

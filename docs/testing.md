# Testing

NDS divides tests by the resources they require. The default build registers
unit and integration tests only. Hardware end-to-end tests are opt-in because
they require a matched Ascend NPU, CANN installation, NPU RNIC, CPU RNIC, and
network configuration.

## Unit tests

Unit tests run without sockets, shared libraries, CANN, RDMA hardware, or an
NPU. They cover the NDS transport and storage codecs, CPU path-MTU policy, RA
QP lifecycle logic through an in-process fake RA API, RMA capabilities, the
AICPU and AIV request ABI layouts, the device QP/WR/connection builders, and
the shared HNS queue/CQE codec used by the device kernels. The QP tests include invalid queue geometry, explicit
QP-mode selection, HCCP-owned completion queues, and missing data-plane queue
descriptors. The HNS codec tests cover receive-WQE layout, queue capacity and
counter wrap, CQ owner phase, send-tail advancement, and CQE decoding.

```sh
ctest --test-dir build --output-on-failure --label-regex '^unit$'
```

## Integration tests

Integration tests exercise boundaries that cross local operating-system or
dynamic-library interfaces without using RDMA hardware. The TCP transport
bootstrap test uses a Unix socket pair. The runtime-loader test builds a fake
shared runtime, opens it with the NDS loader, resolves its C ABI symbols, and
calls them.

When the optional AIV/AICPU toolchains are enabled, `device-toolchain` tests
inspect the generated artifacts and require all verbs and connection symbols.
This prevents the reusable APIs from accidentally becoming private entry-kernel
helpers.

```sh
ctest --test-dir build --output-on-failure --label-regex '^integration$'
```

## End-to-end tests

End-to-end tests run the real NPU client and CPU verbs server against physical
RoCE resources. They are not configured by default and are never run in GitHub
Actions. They must be launched only on the approved target with one bounded
case, a whole-process timeout, and target-specific values held under `.local/`.

To register E2E tests, configure with `NDS_ENABLE_E2E_TESTS=ON` and provide one
or more named CMake command variables. Each value is a CMake list containing a
target-local wrapper command. For example:

```sh
cmake -S . -B build-e2e \
  -DNDS_ENABLE_E2E_TESTS=ON \
  '-DNDS_E2E_AIV_STORAGE_WRITE_COMMAND=/absolute/path/to/.local/run_aiv_write_once.sh' \
  '-DNDS_E2E_AICPU_RECEIVE_CQ_COMMAND=/absolute/path/to/.local/run_aicpu_receive_once.sh'
ctest --test-dir build-e2e --output-on-failure --label-regex '^e2e$'
```

The supported variables and registered test names are:

- `NDS_E2E_HOST_RA_STORAGE_WRITE_COMMAND`: `e2e.host_ra_storage_write`
- `NDS_E2E_AIV_STORAGE_WRITE_COMMAND`: `e2e.aiv_storage_write`
- `NDS_E2E_AICPU_STORAGE_WRITE_COMMAND`: `e2e.aicpu_storage_write`
- `NDS_E2E_AIV_SEND_CQ_COMMAND`: `e2e.aiv_send_cq`
- `NDS_E2E_AICPU_SEND_CQ_COMMAND`: `e2e.aicpu_send_cq`
- `NDS_E2E_AIV_RECEIVE_CQ_COMMAND`: `e2e.aiv_receive_cq`
- `NDS_E2E_AICPU_RECEIVE_CQ_COMMAND`: `e2e.aicpu_receive_cq`
- `NDS_E2E_AIV_RDMA_READ_COMMAND`: `e2e.aiv_rdma_read`
- `NDS_E2E_AICPU_RDMA_READ_COMMAND`: `e2e.aicpu_rdma_read`
- `NDS_E2E_AIV_RDMA_WRITE_COMMAND`: `e2e.aiv_rdma_write`
- `NDS_E2E_AICPU_RDMA_WRITE_COMMAND`: `e2e.aicpu_rdma_write`

The wrapper owns server startup, client invocation, timeout, cleanup, and
validation for one selected mode. Create it locally when needed; it must not be
tracked because it contains target addresses and deployment paths. The
registered tests have a 120-second CTest timeout, share the
`ascend_npu0` resource lock, and carry `e2e`, `requires-ascend-npu`,
`requires-rdma`, `mode-*`, and `operation-*` labels. This permits selecting one
mode or operation while preventing concurrent use of the single NPU.

Configure `NDS_BUILD_HARDWARE_PROBES=ON` to build
`nds_device_ops_client` and `nds_device_ops_server`. This opt-in pair accepts
`--operation send`, `receive`, `read`, or `write`. The send case makes the CPU
post a verbs receive, then validates device `post_send`, send-CQ polling, and the
payload received by the CPU. The receive case makes the NPU post a receive, the
CPU issue one verbs Send, and the NPU validate receive-CQ polling, the CQE WR
ID, and the payload. The read and write cases exchange a test MR descriptor,
invoke the device connection-layer one-sided operation, explicitly poll the
send CQ, and verify the payload at the destination. The executables contain no
target addresses and are not registered with CTest; target-specific bounded
launch commands remain under `.local/`.

On the CANN 9.0.0 target host, bounded probes have directly validated AIV
and AICPU Send posting with SCQ polling and CPU-side payload verification, plus
receive posting with RCQ polling and NPU-side payload verification. Direct
RDMA Read and RDMA Write with SCQ polling and destination-side payload checks
also pass in both modes. Bounded storage Writes have separately validated the
storage command path. These observations do not claim AIV/AICPU storage Read,
concurrency, performance, or another CANN/provider version.

## CI

GitHub Actions installs only standard Linux build dependencies, then runs the
`unit` and `integration` labels. It does not configure device kernels, register
the E2E test, or claim that a hosted runner can validate Ascend hardware.

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
inspect the generated artifacts and require all verbs, connection, and storage symbols.
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
  '-DNDS_E2E_AIV_STORAGE_COMMAND=/absolute/path/to/.local/run_aiv_storage_once.sh' \
  '-DNDS_E2E_AICPU_CONNECTION_COMMAND=/absolute/path/to/.local/run_aicpu_connection_once.sh'
ctest --test-dir build-e2e --output-on-failure --label-regex '^e2e$'
```

The supported variables and registered test names are:

- `NDS_E2E_RA_VERBS_COMMAND`, `NDS_E2E_RA_CONNECTION_COMMAND`, `NDS_E2E_RA_STORAGE_COMMAND`
- `NDS_E2E_AIV_VERBS_COMMAND`, `NDS_E2E_AIV_CONNECTION_COMMAND`, `NDS_E2E_AIV_STORAGE_COMMAND`
- `NDS_E2E_AICPU_VERBS_COMMAND`, `NDS_E2E_AICPU_CONNECTION_COMMAND`, `NDS_E2E_AICPU_STORAGE_COMMAND`

Each command registers `e2e.<backend>_<layer>`. A storage run is the full
storage → connection → verbs composition. Verbs and connection commands are
optional isolation probes and must use the matching typed device request ABI.

The wrapper owns server startup, client invocation, timeout, cleanup, and
validation for one selected mode. Create it locally when needed; it must not be
tracked because it contains target addresses and deployment paths. The
registered tests have a 120-second CTest timeout, share the
`ascend_npu0` resource lock, and carry `e2e`, `requires-ascend-npu`,
`requires-rdma`, `mode-*`, and `operation-*` labels. This permits selecting one
mode or operation while preventing concurrent use of the single NPU.

Hardware results are recorded per target experiment under `.local/`; a
successful build or exported-symbol check is not an end-to-end completion.
Storage tests exercise the composed storage → connection → verbs path, while
verbs and connection probes remain independently selectable.

## CI

GitHub Actions installs only standard Linux build dependencies, then builds and
runs every test registered by the default configuration in both Debug and
Release modes. The default configuration includes all unit and non-hardware
integration tests. It does not configure device kernels, register E2E tests, or
claim that a hosted runner can validate Ascend hardware.

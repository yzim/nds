# Testing

NDS divides tests by the resources they require. The default build registers
unit and integration tests only. Hardware end-to-end tests are opt-in because
they require a matched Ascend NPU, CANN installation, NPU RNIC, CPU RNIC, and
network configuration.

## Formatting

Formatting is pinned to clang-format 18 through `pre-commit`. Install
`pre-commit` with Homebrew or pipx on macOS, or with pipx or the distribution
package manager on Linux. Then enable the optional local check:

```sh
pre-commit install
```

Commits check formatting without modifying files. Apply formatting explicitly
with `scripts/format.sh`, or check the complete tree with
`scripts/check-format.sh`. GitHub Actions runs the same pinned check, so local
hook installation is not required for correctness.

## Unit tests

Unit tests use GoogleTest and run without sockets, shared libraries, CANN, RDMA
hardware, or an NPU. They cover the NDS transport and storage codecs, CPU path-MTU policy, RA
QP lifecycle logic through an in-process fake RA API, RMA capabilities, the
AICPU and AIV request ABI layouts, the AICPU mode-0 package registration contract,
the device QP/WR/connection builders, and
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

Tracked E2E runners live in `tests/e2e/`. Configure with
`NDS_ENABLE_E2E_TESTS=ON`; CMake always registers the RA storage case and also
registers AIV/AICPU cases when their device targets are enabled:

```sh
cmake -S . -B build-e2e \
  -DNDS_ENABLE_E2E_TESTS=ON \
  -DNDS_BUILD_AIV_KERNEL=ON \
  -DNDS_BUILD_AICPU_KERNEL=ON \
  -DNDS_CANN_ROOT=<cann-root>
ctest --test-dir build-e2e -N --label-regex '^e2e$'
```

The runner reads all paths, interface names, addresses, ports, and GID values
from `NDS_E2E_*` environment variables. Their contract and bounded invocation
are documented in `tests/e2e/README.md`; values must remain under `.local/` or
another ignored target configuration. The registered tests have a 120-second
CTest timeout, share the
`ascend_npu0` resource lock, and carry `e2e`, `requires-ascend-npu`,
`requires-rdma`, `mode-*`, and `operation-*` labels. This permits selecting one
mode or operation while preventing concurrent use of the single NPU.

Hardware results are recorded per target experiment under `.local/`; a
successful build or exported-symbol check is not an end-to-end completion.
Storage tests exercise the composed storage → transport → verbs path. The
lower-layer examples are independently runnable with
`-DNDS_BUILD_HARDWARE_PROBES=ON`: `examples/verbs` and
`examples/transport` each build a paired client/server program. Storage remains
an application under `apps/`; its server has no lower-layer probe mode.

## CI

GitHub Actions explicitly configures `NDS_ENABLE_E2E_TESTS=OFF`, then builds and
runs all GoogleTest and non-hardware integration tests in both Debug and Release
modes. It does not configure device kernels or claim that a hosted runner can
validate Ascend hardware.

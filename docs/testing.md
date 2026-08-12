# Testing

NDS divides tests by the resources they require. The default build registers
unit and integration tests only. Hardware end-to-end tests are opt-in because
they require a matched Ascend NPU, CANN installation, NPU RNIC, CPU RNIC, and
network configuration.

## Unit tests

Unit tests run without sockets, shared libraries, CANN, RDMA hardware, or an
NPU. They cover the NDS wire codec, CPU path-MTU policy, RA QP lifecycle logic
through an in-process fake RA API, and the AICPU request ABI layout.

```sh
ctest --test-dir build --output-on-failure --label-regex '^unit$'
```

## Integration tests

Integration tests exercise boundaries that cross local operating-system or
dynamic-library interfaces without using RDMA hardware. The control-plane test
uses a Unix socket pair. The runtime-loader test builds a fake shared runtime,
opens it with the NDS loader, resolves its C ABI symbols, and calls them.

```sh
ctest --test-dir build --output-on-failure --label-regex '^integration$'
```

## End-to-end tests

End-to-end tests run the real NPU client and CPU verbs server against physical
RoCE resources. They are not configured by default and are never run in GitHub
Actions. They must be launched only on the approved target with one bounded
case, a whole-process timeout, and target-specific values held under `.local/`.

To register an E2E test, configure with `NDS_ENABLE_E2E_TESTS=ON` and provide
`NDS_E2E_COMMAND` as a CMake list containing a target-local wrapper command:

```sh
cmake -S . -B build-e2e \
  -DNDS_ENABLE_E2E_TESTS=ON \
  '-DNDS_E2E_COMMAND=/absolute/path/to/.local/run_e2e_once.sh'
ctest --test-dir build-e2e --output-on-failure --label-regex '^e2e$'
```

The wrapper owns server startup, client invocation, timeout, cleanup, and
validation for one selected mode. It must not be tracked because it contains
target addresses and deployment paths. The registered test has a 120-second
CTest timeout and labels `e2e`, `requires-ascend-npu`, and `requires-rdma`.

## CI

GitHub Actions installs only standard Linux build dependencies, then runs the
`unit` and `integration` labels. It does not configure device kernels, register
the E2E test, or claim that a hosted runner can validate Ascend hardware.

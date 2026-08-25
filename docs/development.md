# Development

Build and validate NDS only on the approved aarch64 CANN target. The Mac is
source and Git authority only. Keep target paths, addresses, logs, and
operational commands under ignored `.local/` files.

## C++ conventions

NDS is C++20. Use `.cc` for implementation files. Use `.hh` for internal C++
headers, including NDS-owned wire records. Use `.h` only for device, loader, or
future public-SDK ABI boundaries; keep those C-compatible when the boundary
requires C consumers. Reserve `extern "C"` for
vendor APIs and loadable device entrypoints that require unmangled symbol
names. Format with the repository
`.clang-format` configuration.

- Name every NDS-owned C++ class, struct, and enum in `UpperCamelCase`. Use
  platform-required spellings only for vendor API declarations and externally
  named device entrypoints.
- Pass values, `const T&` inputs, or pointers. Do not use non-const lvalue
  references for output or in/out parameters.
- Return newly produced values as `Result<T>`, not through output pointers.
- Use stored non-owning mutable dependencies as pointers, not references.
- Operations that can fail during normal execution return `nds::Result<T>`.
  Use `nds::unexpected(...)` for failures and propagate an existing `Error`
  directly.
- Do not combine boolean success with a side-channel error state. C-compatible
  loader structs may retain ABI-required diagnostic buffers only internally;
  translate them at the C++ boundary.
- Keep resources alive through the execution-mode completion point and tear
  them down in reverse initialization order.
- Use `nds::log` for executable diagnostics and CLI11 for command-line
  declaration and cross-option validation. Use `npu-client` and `cpu-server`
  component names unless an application replaces the logger.

## Formatting and local tests

Formatting is pinned to clang-format 18 through `pre-commit`.

```sh
pre-commit install
scripts/format.sh
scripts/format.sh --check
```

The default target build runs unit and non-hardware integration tests only:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
ctest --test-dir build --output-on-failure --label-regex '^unit$'
ctest --test-dir build --output-on-failure --label-regex '^integration$'
```

Unit tests require no sockets, shared libraries, CANN, RDMA hardware, or NPU.
They cover codecs, CPU MTU policy, fake RA lifecycle behavior, RMA capability,
device ABI layouts, AICPU package registration, and device queue/CQE codecs.
Integration tests cover the Unix-socket bootstrap and a fake dynamically loaded
runtime; optional device-toolchain tests inspect generated symbols.

GitHub Actions builds Debug and Release with `NDS_ENABLE_E2E_TESTS=OFF`. It
does not configure device kernels or claim hardware validation.

## Hardware validation

E2E tests are opt-in and run only on the approved target with one NPU,
`sudo -n`, one bounded case, and a whole-process timeout. Record each failed
experiment before changing another variable. Do not run blindly repeated
experiments.

```sh
cmake -S . -B build-e2e \
  -DNDS_ENABLE_E2E_TESTS=ON \
  -DNDS_BUILD_AIV_KERNEL=ON \
  -DNDS_BUILD_AICPU_KERNEL=ON \
  -DNDS_CANN_ROOT=<cann-root>
ctest --test-dir build-e2e -N --label-regex '^e2e$'
```

`tests/e2e/README.md` defines the `NDS_E2E_*` environment contract. Registered
tests have a 120-second timeout, share the `ascend_npu0` resource lock, and
have mode and operation labels. A package build, stream synchronization,
provider resolution, or HCCP internal CQ activity is not storage completion.
Keep hardware claims tied to recorded bounded target experiments.

The paired lower-layer examples build with `-DNDS_BUILD_HARDWARE_PROBES=ON`.
Storage remains the application-level test path under `apps/`.

Before interpreting or extending a hardware performance result, read the
tracked [benchmark report](benchmark.md), then the current ignored ledgers
`.local/status.md` and the relevant `.local/logs/` evidence. The report is the
durable summary; local state records the active handoff and target-specific
raw evidence behind it.

## PyTorch wrapper

The optional `_nds_torch` extension in `src/torch` owns runtime, endpoint, QP,
registered-memory, and storage-session lifetime in C++. It requires compatible
PyTorch, `torch_npu`, and CANN on the target:

```sh
source <cann-root>/set_env.sh
cmake -S . -B build-torch \
  -DNDS_BUILD_TORCH_WRAPPERS=ON \
  -DPython3_EXECUTABLE=<python3.10> \
  -DCMAKE_PREFIX_PATH=<torch-cmake-prefix> \
  -DNDS_CANN_ROOT=<cann-root>
cmake --build build-torch --target _nds_torch --parallel
```

Add `build/bin` to `PYTHONPATH`, configure the CANN environment, import
`torch_npu`, and select the device before constructing `_nds_torch.Session`.
`Session` exposes `read_`, `write`, and `capacity`; calls are serialized over
one connected storage session. It accepts nonempty contiguous CPU tensors and
copies them through NDS-owned NPU allocations. Direct `torch_npu` tensor
registration is intentionally unsupported pending an allocator-lifetime and
stream-synchronization contract.

# NDS Contributor Guide

## Purpose

NDS is a small interoperability project for one Ascend NPU RNIC and one
CPU-side RoCE RNIC. It owns its TCP transport bootstrap, storage protocol, and
NPU backends; it uses the CANN runtime installed on the target.

The endpoint roles are fixed:

- The NPU client uses dynamically loaded CANN RA APIs.
- The CPU server uses ordinary `libibverbs`.
- The normal path does not initialize HCOMM, HCCL, TSD, a rank table, or a
  second NPU.

Start with [README.md](README.md) for the runnable baseline. Technical choices
belong in `docs/`: [HCCP QP and MR lifecycle](docs/hccp-resources.md),
[mode overview](docs/modes.md), [host RA](docs/host-ra.md), [AIV](docs/aiv.md),
[AICPU](docs/aicpu.md), and
[linkage policy](docs/linkage.md), [testing](docs/testing.md), and the
[protocol roadmap](docs/roadmap.md). Source ownership and dependency direction
are defined in [architecture](docs/architecture.md).

## Non-negotiable rules

- Never build or test NDS on this Mac. The Mac is the source and Git authority
  only. Build, inspect CANN/HCOMM/HCCL sources, and run hardware validation on
  `node200`.
- Synchronize source without `.git`, `.local`, or build output. Keep target
  paths, addresses, logs, and operational commands under ignored `.local/`.
- Use one NPU, `sudo -n`, a whole-process timeout, and no blind retry for a
  hardware experiment. Record a failed experiment before changing the next
  variable.
- Keep real-NPU end-to-end tests opt-in. GitHub Actions must run only unit and
  non-hardware integration tests.
- Do not commit addresses, credentials, private keys, SSH configuration, or
  operational logs.
- Do not link, vendor, or copy private CANN, HCCP, HCOMM, HCCL, or HNS provider
  implementation code. Dynamically load the narrow CANN/RA boundary; link only
  platform dependencies and the CPU-only `libibverbs` target.
- The host process must not load `libhns-rdmav25.so`. It is an NPU-side provider
  dependency used only by the standard-CP1 AICPU kernel.

## Source and ABI rules

- CANN 9.0.0 and the matching HCOMM checkout at `../hcomm` are the source basis
  for lifecycle and ABI decisions. The installed CANN libraries on the target
  are authoritative at runtime.
- Keep runtime loading, library names, symbol resolution, and ABI checks in the
  loader modules. Missing required symbols must fail closed with a useful error.
- Preserve the endpoint-local dependency direction: application -> storage
  protocol -> transport connection -> backend. `src/common/transport.*` owns
  shared endpoint metadata, TCP bootstrap, and MTU policy;
  `src/common/protocol.*` owns shared storage records. Backend and transport
  code must not depend on storage command semantics.
- Keep the TCP transport bootstrap independent of RA/HCCP private objects. Exchange
  only NDS-owned versioned wire records with the CPU endpoint. Exchange QPN,
  PSN, GID, transport settings, and memory address/length/rkey; never exchange
  HCCP QP or MR handles, AI-QP descriptors, queue addresses, or provider
  objects.
- Preserve explicit ownership order: initialize context, create rdev/QP,
  register memory, submit, verify, deregister, destroy QP/rdev, then close the
  runtime. Keep QPs, MRs, and NPU allocations alive until host CQ completion
  plus the current CPU test-harness check for host RA, or until that check for
  AI-QP modes. Do not describe the CPU check as a general completion API.
- NDS is C++20. Use native C++ ownership and `nullptr` in compiled C++ code.
  Use `.cc` for C++ sources and `.hh` for C++ headers. Keep `.h` headers
  C-compatible only where a runtime or device boundary needs it.
- Function parameters must be values, `const T&` inputs, or pointers. Do not
  use non-const lvalue-reference parameters: expose output and in/out values
  as `T*` so mutation is visible at the call site. Stored non-owning mutable
  dependencies are pointers, not references. The only exception is a C++
  language-required special member; prefer deleting it when it is unnecessary.
- Use `nds::Result<T>` for C++ operations that can fail during normal runtime
  execution. Construct and propagate outcomes only through the helpers in
  `nds/result.hh`. Return `nds::Error` with a category and message. No NDS API
  may accept a caller-owned diagnostic string or character buffer. C wire
  codecs return typed result enums. Vendor loader structs may retain only their
  embedded ABI error fields internally; translate those diagnostics at the NDS
  C++ boundary.
- Format C++ with the repository `.clang-format` configuration.
- Use the `nds::log` facade backed by `spdlog` for executable diagnostics. Do
  not add direct `stdout`, `stderr`, `printf`, or `perror` logging. Keep the
  component names `npu-client` and `cpu-server`; applications may replace their
  spdlog sink through `nds::log::set_logger`.
- Use CLI11 for executable command-line parsing. Keep option declarations and
  cross-option semantic validation at the executable boundary.

## Network safety

Before changing a host network interface, confirm the active SSH management
path uses another interface. Persist intended network changes through the host
network configuration system and verify connectivity in both directions.

## Validation scope

Do not represent a package build, stream synchronization, provider resolution,
or HCCP internal AI-QP CQ handling as an NDS command completion. Completion
ownership is mode-specific and documented in `docs/modes.md`. Keep claims of
hardware validation tied to recorded, bounded target experiments.

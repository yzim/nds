# NDS Contributor Guide

## Purpose

NDS is a small interoperability project for one Ascend NPU RNIC and one
CPU-side RoCE RNIC. It owns its control plane and submission implementations;
it uses the CANN runtime installed on the target.

The endpoint roles are fixed:

- The NPU client uses dynamically loaded CANN RA APIs.
- The CPU server uses ordinary `libibverbs`.
- The normal path does not initialize HCOMM, HCCL, TSD, a rank table, or a
  second NPU.

Start with [README.md](README.md) for the runnable baseline. Technical choices
belong in `docs/`: [mode overview](docs/modes.md), [host RA](docs/host-ra.md),
[AIV](docs/aiv.md), [AICPU](docs/aicpu.md), and
[linkage policy](docs/linkage.md).

## Non-negotiable rules

- Never build or test NDS on this Mac. The Mac is the source and Git authority
  only. Build, inspect CANN/HCOMM/HCCL sources, and run hardware validation on
  `node200`.
- Synchronize source without `.git`, `.local`, or build output. Keep target
  paths, addresses, logs, and operational commands under ignored `.local/`.
- Use one NPU, `sudo -n`, a whole-process timeout, and no blind retry for a
  hardware experiment. Record a failed experiment before changing the next
  variable.
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
- Keep the TCP control plane independent of RA/HCCP private objects. Exchange
  only NDS-owned versioned wire records with the CPU endpoint. Exchange QPN,
  PSN, GID, transport settings, and memory address/length/rkey; never exchange
  HCCP QP or MR handles, AI-QP descriptors, queue addresses, or provider
  objects.
- Preserve explicit ownership order: initialize context, create rdev/QP,
  register memory, submit, verify, deregister, destroy QP/rdev, then close the
  runtime. Keep QPs, MRs, and NPU allocations alive until host CQ completion
  plus CPU verification for host RA, or CPU verification for AI-QP modes.
- NDS is C++20. Use native C++ ownership and `nullptr` in compiled C++ code.
  Keep ABI and wire headers C-compatible only where a runtime or device boundary
  needs it.

## Network safety

Before changing a host network interface, confirm the active SSH management
path uses another interface. Persist intended network changes through the host
network configuration system and verify connectivity in both directions.

## Validation scope

The validated data path is one bounded NPU-to-CPU RDMA Write. The CPU server
currently does not provide the receive or remote-memory setup required to claim
end-to-end RDMA Read or Send support. Do not represent a package build, stream
synchronization, or provider resolution as an RNIC completion. Completion
ownership is mode-specific and documented in `docs/modes.md`.

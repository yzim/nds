# NDS agent notes

## Purpose

NDS is a small, distributable integration project for exercising a RoCE path between a Huawei Ascend NPU RNIC and a CPU-side RoCE RNIC. It will use the HCCP runtime installed by CANN, while owning its own implementation, build system, and documentation.

## Delivery goal

Build an application/library that can:

1. discover and dynamically load the installed HCCP shared library at runtime;
2. resolve and validate the HCCP ABI symbols needed for initialization, connection lifecycle, and memory registration;
3. establish a RoCE connection between an NPU RNIC and a host RNIC;
4. register and deregister application memory through the HCCP interface;
5. exchange the necessary peer/connection metadata through a transport that we implement; and
6. demonstrate a verifiable data-path operation with robust cleanup and useful diagnostics.

The implementation must be portable across compatible CANN installations: do **not** statically link against, vendor, or copy private HCCP implementation code.

## Implementation principles

- Use the open-source HCOMM/HCCP tree only as a behavioral and ABI-reference aid. Do not copy its source or implementation into this repository.
- Link only platform dependencies and the CPU-only `libibverbs` target. The portable NPU client dynamically loads the selected CANN root; see `docs/linkage.md` for the per-library ownership policy.
- Use `dlopen(3)`, `dlsym(3)`, and `dlerror(3)` (or the platform equivalent) for the CANN/HCCP runtime boundary.
- Keep all library names, search paths, symbol names, ABI/version validation, and optional-symbol handling behind a narrow runtime-loader module.
- Declare only the ABI-facing types/function-pointer signatures actually required by the project, and document the CANN/HCCP versions against which they were validated.
- Fail closed: if a required library, symbol, or version is missing or incompatible, emit a precise diagnostic and do not start the data path.
- Separate control plane (peer discovery and metadata exchange) from data plane (HCCP/RoCE operations).
- Make lifecycle ownership explicit: initialization, connection creation, memory registration, operation, deregistration, connection teardown, and library unload.
- The normal interoperability path is **one NPU and one CPU RNIC**. It must not initialize HCOMM/HCCL, consume a rank table, or use a second NPU.
- The direct NPU lifecycle is `aclInit` → `aclrtSetDevice` → `rtOpenNetService(--hdcType=18)` → `RaInit` → RA rdev/QP work → `RaDeinit` → runtime close → `aclFinalize`.
- HCOMM and TSD loaders remain diagnostic/reference tools only. Do not introduce them into the direct CPU-peer path without a separately validated ownership model.
- Never put host-specific addresses, credentials, private keys, SSH configuration, or operational change records in tracked files.

## Proposed work sequence

1. **Inventory runtime ABI** — locate installed HCCP shared objects and public/exported symbols; capture library and CANN version evidence.
2. **Map reference behavior** — inspect the open-source HCCP source only to understand initialization order, mandatory parameters, opaque handles, connection metadata, and teardown order.
3. **Define our adapter** — write a minimal independent HCCP dynamic-loader header/module with strongly typed function pointers and capability checks.
4. **Implement control plane** — select and implement a simple peer metadata exchange mechanism independent of HCCP internals.
5. **Implement connection and memory lifecycle** — create a connection, register test buffers, execute the minimal supported operation, then cleanly release all resources.
6. **Validate incrementally** — first loader/symbol checks, then NPU-local setup, then peer connection, memory registration, transfer, bidirectional validation, and negative/error cases.
7. **Package** — provide a documented build, runtime library-discovery configuration, example invocation, and compatibility matrix.

## Public software baseline

- Accelerator type: Huawei Ascend 910B3.
- Driver package version: `25.5.1`.
- CANN Toolkit version: `9.0.0`.
- The matching HCOMM source checkout is available in the sibling directory `../hcomm`, at tag `v9.0.0`.
- HCCP reference source is located in `../hcomm/src/platform/hccp`.

## Useful diagnostic commands

```sh
# NPU status, utilization, and processes
sudo npu-smi info

# HCCS link status for a card/chip
sudo npu-smi info -t hccs -i <card-id> -c <chip-id>

# Read an NPU RNIC address
sudo hccn_tool -i <npu-id> -ip -g

# Test reachability from an NPU RNIC
sudo hccn_tool -i <npu-id> -ping -g address <destination-ip> pkt 64

# Inspect host-side RDMA devices and their netdev mappings
rdma link show

# Inspect ELF dynamic symbols without executing the library
readelf -Ws <library.so>
nm -D --defined-only <library.so>
```

## Local-only information

- Put environment-specific addresses, host inventory, interface mappings, credentials, library locations discovered on a particular machine, and operational change records under `.local/`.
- `.local/` is ignored by Git and must never be committed.
- Do not place passwords, private keys, or SSH configuration in tracked files.

## Networking safety

Before changing a host network interface, verify that the active SSH management path uses a different interface. Persist intended changes through the host's network configuration system, then validate connectivity in both directions.

## Target interoperability model

The endpoint roles are intentionally asymmetric:

- **NPU endpoint:** exactly one selected NPU uses dynamically loaded HCCP/RA APIs as the RoCE **client**.
- **CPU endpoint:** the host-side Mellanox RNIC uses ordinary `libibverbs` as the RoCE **server**.
- **Transport:** both endpoints communicate directly over RoCE; HCCP is not required on the CPU endpoint.

Treat the interoperation boundary as an RDMA wire-compatibility exercise. Our control plane must exchange exactly the connection and memory metadata required by the selected QP mode, while the NPU-facing code obtains/consumes its side through HCCP and the CPU-facing code uses `libibverbs`.

## Validated QP-only milestone

The direct one-NPU/one-CPU path has been validated through QP establishment only: the CPU QP reaches `RTS` and the NPU-side `RaTypicalQpModify` succeeds after exchange of QPN, PSN, GID, GID index, and transport parameters. This milestone registers no memory and posts no work request. `NETWORK_OFFLINE` rdev setup uses the source-verified HCCP `NOTIFY` value (`1`), not `NO_USE` (`0`).

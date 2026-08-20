# NPU Direct Storage (NDS)

NDS enables an Ascend NPU device to connect through its own RoCE RNIC to a
remote RoCE endpoint and submit storage requests. The NPU machine acts as the
client. The CPU machine acts as the server, owns a memory-backed namespace,
and performs the data movement through `libibverbs`.

## Architecture

```mermaid
flowchart LR
  subgraph client["NPU machine (acts as client)"]
    direction TB
    CA["Application<br/>configuration and verification"]
    CS["StorageClient<br/>Read/Write and protocol sequencing"]
    CT["Transport<br/>registered buffers and one RC QP"]
    CW["RMA<br/>post and completion operations"]
    CA --> CS --> CT --> CW
  end

  subgraph server["CPU machine (acts as server)"]
    direction TB
    SA["Application<br/>namespace configuration"]
    SP["Storage protocol<br/>command decode and data movement"]
    ST["Transport connection<br/>CPU machine endpoint metadata and one RC QP"]
    SB["Backend<br/>libibverbs"]
    SA --> SP --> ST --> SB
  end

  R["RoCE RC QP"]
  CW -->|"1. RDMA Send: storage command"| R --> SB
  SB -->|"2. RDMA Read or Write: application data"| R --> CS
  SB -->|"3. RDMA Write: terminal completion record"| R --> CS
```

The NPU client uses one dependency direction: application to `StorageClient`
to `Transport` to RMA. The current RMA binding is RoCE QP/CQ; the CPU machine owns the
namespace and data movement. See [design](docs/design.md).

## NPU execution modes

NDS provides three execution modes for NPU-side work requests.

Here, **NPU device** means the Ascend accelerator in the NPU machine, and
**host CPU** means the CPU in that machine.

| Execution mode | RDMA-post execution site | Local completion | Guide |
|---|---|---|---|
| `ra` | Host CPU: RA Send plus runtime doorbell | RA CQ is available | [RA](docs/design.md#ra) |
| `aiv` | NPU AIV: direct SQ/RQ/CQ and doorbell access | Caller-owned SCQ/RCQ polling | [AIV](docs/design.md#aiv) |
| `aicpu` | NPU CP1: provider symbols first, queue-address fallback | Caller-owned SCQ/RCQ polling | [AICPU](docs/design.md#aicpu) |

The table identifies where the RDMA post executes, not where it is invoked.
An AIV or AICPU operator may be invoked from the host CPU or from another
operator on the NPU device.

The current validation executable uses the host CPU for lifecycle, QP/MR setup,
operator launch, and completion observation. AIV and AICPU also expose
device-callable transport and storage Read/Write APIs for use by other NPU
operators.

The execution modes share the same host HCCP rdev/QP and memory-registration
control path. Their QP types, post paths, CQ ownership, and current limitations
differ; see [design](docs/design.md).

The CPU actively polls its verbs CQ for both the command Receive and its
signaled completion Write. For a storage Write, arrow 2 is an RDMA Read issued
by the CPU machine from the application buffer on the NPU device; for a storage
Read, it is an RDMA Write issued by the CPU machine to that buffer. The NPU
machine treats arrow 3's completion record, also written by the CPU machine,
as the command result. An operator launch, HCCP internal AI-QP CQ processing,
and an RA local CQE are not this protocol completion.

## Repository layout

```text
src/              Reusable client, server, common, and Torch wrapper libraries
apps/             Complete NDS applications, including storage and Torch
examples/         Paired client/server instances for lower API layers
benchmarks/       Reserved for opt-in performance workloads
tests/            Unit/integration tests and opt-in hardware validation
docs/             Consolidated design, development, roadmap, and reference guides
```

The examples demonstrate only the lower layers. Each of `examples/verbs` and
`examples/transport` contains a paired client and server; do not use one generic
server with flags for unrelated layers. Storage is the complete NDS application
and remains under `apps/`, alongside the Python Torch application which imports
the reusable `_nds_torch` extension from `src/torch`. The C++ code in `src/`
remains library code.

The headers in `src/include/nds/` are shared internal interfaces, named
with an `nds/` prefix to avoid collisions. NDS does not currently install an
external SDK.

Host C++ operations return `nds::Result<T>`. NDS uses this for expected
runtime, RA, RMA, transport, and protocol failures rather than exceptions or
a separate boolean and error-output parameter.

## Build

Build and test only on the matching aarch64 CANN target. Do not build or test
on a development Mac.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Device-kernel builds and mode-specific invocation are documented in the mode
guides. Keep target paths, addresses, logs, and operational commands in ignored
`.local/` files.

## Documentation

NDS's QP creation, queue manipulation, doorbell, and provider-ABI knowledge
was learned from public HCOMM, HCCL, rdma-core, and Ascend repositories. See
the [reference basis](docs/references.md) for the
specific sources and limits.

- [Design](docs/design.md): architecture, wire boundary, lifecycle, completion,
  runtime ABI, and RA/AIV/AICPU execution modes.
- [Development](docs/development.md): C++ conventions, testing, hardware
  validation, and the optional PyTorch wrapper.
- [Roadmap](docs/roadmap.md): next protocol and concurrency work.
- [Reference basis](docs/references.md): public source material and evidence
  limits.

# NPU Direct Storage (NDS)

NDS is a one-NPU/one-CPU RoCE storage-protocol prototype. The NPU client
submits requests through Host RA, AIV, or AICPU; the CPU `libibverbs` server
owns a memory-backed namespace and performs the data movement.

NDS's QP creation, queue manipulation, doorbell, and provider-ABI knowledge
was learned from public HCOMM, HCCL, rdma-core, and Ascend repositories. We are
grateful to their maintainers and contributors; see the
[open-source reference basis](docs/open-source-references.md).

## Architecture

```text
Application -> Storage protocol -> Transport connection -> Backend
```

The backend owns QP/MR operations and posting; the transport owns one connected
QP; the protocol sequences command, data movement, and completion; and the
application owns configuration and verification. See [architecture](docs/architecture.md).

## NPU backends

NDS provides three backends for the NPU endpoint.

| Backend | `post_send` | Local completion | Guide |
|---|---|---|---|
| `host-ra` | NPU-side host CPU: RA Send plus runtime doorbell | Host RA CQ is available | [Host RA](docs/npu-backends.md#host-ra) |
| `aiv` | NDS AIV kernel writes one Send WQE and doorbell | HCCP internal AI-QP handling; future AIV backend handling | [AIV](docs/npu-backends.md#aiv) |
| `aicpu` | NDS standard-CP1 kernel calls NPU-side provider `post_send` | HCCP internal AI-QP handling; future AICPU backend handling | [AICPU](docs/npu-backends.md#aicpu) |

The backends share the same HCCP rdev/QP connection and
memory-registration lifecycle. Their QP types, post paths, CQ ownership, and
current limitations differ; see [HCCP QP and MR lifecycle](docs/hccp-resources.md)
and [NPU backends](docs/npu-backends.md).

## Storage path

```text
NPU RDMA Send(command) -> CPU Receive
CPU storage Write -> CPU RDMA Read(NPU application buffer)
CPU storage Read  -> CPU RDMA Write(NPU application buffer)
CPU RDMA Write(completion record) -> NPU internal completion buffer
```

The CPU actively polls its verbs CQ for both the command Receive and its
signaled completion Write. The NPU treats the CPU-written completion record as
the command result. A backend launch, HCCP internal AI-QP CQ processing, and a
host-RA local CQE are not this protocol completion.

## Repository layout

```text
src/client/       NPU-attached client: main, protocol, transport, and backends
src/server/       CPU-side server: main, protocol, transport, and verbs backend
src/common/       Shared transport bootstrap/metadata, storage ABI, and logging
tests/            Unit tests and a test-only runtime fixture
docs/             Resource lifecycle, modes, linkage, and implementation guides
```

The headers in `src/common/include/nds/` are shared internal interfaces, named
with an `nds/` prefix to avoid collisions. NDS does not currently install an
external SDK.

Host C++ operations return `nds::Result<T>`. NDS uses this for expected
runtime, RA, verbs, transport, and protocol failures rather than exceptions or
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

**System design**

- [Architecture](docs/architecture.md): ownership boundaries and the storage
  protocol.
- [HCCP QP and MR lifecycle](docs/hccp-resources.md): resource ownership,
  bootstrap, and teardown.
- [NPU backends](docs/npu-backends.md): Host RA, AIV, and AICPU posting paths.

**Runtime and evidence**

- [Runtime libraries and ABI](docs/runtime-abi.md): linked libraries, dynamic
  loaders, provider boundary, and runtime invariants.
- [Open-source reference basis](docs/open-source-references.md): public source
  material that informed NDS, with its limits.

**Validation and future work**

- [Testing](docs/testing.md): unit, integration, and opt-in hardware tests.
- [Protocol roadmap](docs/roadmap.md): next protocol and concurrency work.

# NDS

NDS is a storage-protocol prototype between two RoCE endpoints:

- **NPU client:** creates HCCP resources and sends storage commands through
  host RA, AIV, or AICPU.
- **CPU server:** owns a memory-backed namespace and uses `libibverbs`.

The NPU client sends a storage command. The CPU server receives it and moves
the command's data with RDMA Read or RDMA Write against a memory-backed
namespace. The CPU then writes a completion record to NPU memory.

NDS creates the NPU HCCP rdev/QP and registers NPU memory through CANN RA. The
CPU independently creates an RC QP and registers memory through `libibverbs`.
`src/common/transport.*` uses TCP only to bootstrap the connected RC QPs and
exchange NDS-owned endpoint metadata. `src/common/protocol.*` defines the
storage bootstrap, namespace, command, and completion records; storage
commands and data use RoCE.

This is a one-NPU/one-CPU path. It is not an HCCL job: it does not initialize
HCOMM or HCCL, consume a rank table, or require a second NPU. The CPU endpoint
is CANN-free.

## NPU backends

NDS provides three backends for the NPU endpoint.

| Backend | `post_send` | Local completion | Protocol completion | Guide |
|---|---|---|---|
| `host-ra` | NPU-side host CPU: RA Send plus runtime doorbell | Host RA CQ is available | NPU host polls the CPU-written completion record | [Host RA](docs/host-ra.md) |
| `aiv` | NDS AIV kernel writes one Send WQE and doorbell | HCCP internal AI-QP handling; future AIV backend handling | NPU host polls the CPU-written completion record | [AIV](docs/aiv.md) |
| `aicpu` | NDS standard-CP1 kernel calls NPU-side provider `post_send` | HCCP internal AI-QP handling; future AICPU backend handling | NPU host polls the CPU-written completion record | [AICPU](docs/aicpu.md) |

The backends share the same HCCP rdev/QP connection and
memory-registration lifecycle. Their QP types, post paths, CQ ownership, and
current limitations differ; see [HCCP QP and MR lifecycle](docs/hccp-resources.md)
and [NPU backends](docs/modes.md).

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

## Architecture

Each endpoint follows the same dependency direction:

```text
Application -> Storage protocol -> Transport connection -> Backend
```

The backend owns QP/MR operations and hardware-specific posting. The transport
owns one connected QP and exposes connection-level operations. The storage
protocol sequences command Send, CPU data movement, and completion. The
application owns CLI configuration, buffers, and workload verification. See
[architecture](docs/architecture.md) for the concrete source boundaries.

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

Host C++ operations return `nds::Result<T>`, backed by
[`tl::expected`](https://github.com/TartanLlama/expected). NDS uses this for
expected runtime, RA, verbs, transport, and protocol failures rather than
exceptions or a separate boolean and error-output parameter.

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

## Logging

NDS uses named [`spdlog`](https://github.com/gabime/spdlog) loggers: `npu-client`
and `cpu-server`. The command-line tools accept `--log-sink
stderr|stdout|syslog|none` and `--log-level trace|debug|info|warn|error|critical|off`.
The default is `stderr` at `info`; `syslog` lets a host logging agent collect
records without parsing terminal output.

Code embedding an NDS component can install a logger with any spdlog sink
before use:

```cpp
nds::log::set_logger("npu-client", application_logger);
```

NDS does not own or require a particular log collector. The CMake build requires
the system `spdlog` and CLI11 development packages.

## Documentation

- [HCCP QP and MR lifecycle](docs/hccp-resources.md)
- [Architecture](docs/architecture.md)
- [NPU backends](docs/modes.md)
- [Host RA](docs/host-ra.md)
- [AIV](docs/aiv.md)
- [AICPU](docs/aicpu.md)
- [Linkage and runtime ABI](docs/linkage.md)
- [Testing](docs/testing.md)
- [Protocol roadmap](docs/roadmap.md)

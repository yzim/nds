# NDS

NDS is a storage-protocol prototype between two RoCE endpoints:

- **NPU client:** creates HCCP resources and submits storage commands through
  host RA, AIV, or AICPU.
- **CPU server:** owns a memory-backed namespace and uses `libibverbs`.

NDS creates the NPU HCCP rdev/QP and registers NPU memory through CANN RA. The
CPU creates an RC QP and registers its local namespace through `libibverbs`.
TCP only bootstraps QP metadata, namespace capacity, and the NPU completion-MR
descriptor; HCCP handles, queue addresses, and provider objects never cross it.

This is a one-NPU/one-CPU path. It is not an HCCL job: it does not initialize
HCOMM or HCCL, consume a rank table, or require a second NPU. The CPU endpoint
is CANN-free.

## Submission modes

NDS supports three ways to submit the NPU storage-command Send.

| Mode | `post_send` | Protocol completion | Guide |
|---|---|---|---|
| `host-ra` | NPU-side host CPU: RA Send plus runtime doorbell | NPU host polls the NPU completion record copied from device memory | [Host RA](docs/host-ra.md) |
| `aiv` | NDS AIV kernel writes one Send WQE and doorbell | NPU host polls the NPU completion record copied from device memory | [AIV](docs/aiv.md) |
| `aicpu` | NDS standard-CP1 kernel calls NPU-side provider `post_send` | NPU host polls the NPU completion record copied from device memory | [AICPU](docs/aicpu.md) |

The submission implementations share the same HCCP rdev/QP connection and
memory-registration lifecycle. Their QP types, post paths, CQ ownership, and
current limitations differ; see [HCCP QP and MR lifecycle](docs/hccp-resources.md)
and [submission modes](docs/modes.md).

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
src/common/       Protocol records, TCP bootstrap, logging, and MTU policy
src/npu_client/   NPU RA lifecycle, backend posters, and NPU transport session
src/cpu_server/   CPU verbs connection and storage transport executor
tests/            Unit tests and a test-only runtime fixture
docs/             Resource lifecycle, modes, linkage, and implementation guides
```

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
- [Submission modes](docs/modes.md)
- [Host RA](docs/host-ra.md)
- [AIV](docs/aiv.md)
- [AICPU](docs/aicpu.md)
- [Linkage and runtime ABI](docs/linkage.md)
- [Testing](docs/testing.md)
- [Protocol roadmap](docs/roadmap.md)

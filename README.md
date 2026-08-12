# NDS

NDS connects two RoCE endpoints directly:

- **NPU client:** creates an HCCP rdev and RC QP, registers NPU source memory,
  and submits RDMA requests through host RA, AIV, or AICPU.
- **CPU server:** creates an RC QP and destination MR with `libibverbs`.

The endpoints exchange QPN, PSN, GID, port, and retry metadata to connect the
two RC QPs. For the current NPU-to-CPU Write path, the CPU sends its destination
address, length, and rkey; the NPU uses its HCCP MR lkey as the local SGE key.
No HCCP or verbs handles cross the control-plane boundary.

This is a one-NPU/one-CPU path. It is not an HCCL job: it does not initialize
HCOMM or HCCL, consume a rank table, or require a second NPU. The CPU endpoint
is CANN-free.

## Submission modes

NDS supports three ways to submit an NPU RDMA request. The table identifies
only the request submitter and the current owner of the NPU send CQ.

| Mode | Submission | NPU send-CQ handling | Guide |
|---|---|---|---|
| `host-ra` | NPU-side host CPU | NPU-side host CPU polls the CQ | [Host RA](docs/host-ra.md) |
| `aiv` | NDS AIV kernel | HCCP handles the AI-QP CQ internally on the NPU | [AIV](docs/aiv.md) |
| `aicpu` | NDS standard-CP1 AICPU kernel | HCCP handles the AI-QP CQ internally on the NPU | [AICPU](docs/aicpu.md) |

The submission implementations share the same HCCP rdev/QP connection and
memory-registration lifecycle. Their QP types, post paths, CQ ownership, and
current limitations differ; see [HCCP QP and MR lifecycle](docs/hccp-resources.md)
and [submission modes](docs/modes.md).

## Architecture

```text
NPU client                                      CPU server
----------                                      ----------
CANN RA / HCCP                                  libibverbs
  create rdev, QP, and source MR                  create QP and destination MR
       |                                                |
       +------ NDS TCP peer and memory exchange -------+
                              |
                     selected NPU submission mode
                              |
                    direct RoCE data transfer
```

## Repository layout

```text
src/common/       TCP control plane, NDS wire format, and MTU policy
src/npu_client/   NPU RA lifecycle, runtime loaders, submission modes, and probes
src/cpu_server/   CPU `libibverbs` endpoint
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

## Documentation

- [HCCP QP and MR lifecycle](docs/hccp-resources.md)
- [Submission modes](docs/modes.md)
- [Host RA](docs/host-ra.md)
- [AIV](docs/aiv.md)
- [AICPU](docs/aicpu.md)
- [Linkage and runtime ABI](docs/linkage.md)

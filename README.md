# NPU Direct Storage (NDS)

NDS is a storage interoperability project for one Ascend NPU RNIC client and
one CPU-side RoCE server. The NPU submits storage requests; the CPU owns the
memory-backed namespace and data movement through `libibverbs`.

## Basis

Its design, lifecycle, and ABI understanding are based on public open-source
projects ([HCOMM](https://gitcode.com/cann/hcomm),
[HCCL](https://gitcode.com/cann/hccl), and
[rdma-core](https://github.com/linux-rdma/rdma-core)) and Ascend documentation.
The installed CANN libraries on the target remain authoritative at runtime. See
[reference basis](docs/references.md).

## API Matrix

NDS supports three execution modes across three API layers:

| Layer | APIs | `ra` | `aiv` | `aicpu` |
|---|---|---|---|---|
| Verbs | `PostSend`, `PostRecv`, `PollCq` | ✓ | ✓ | ✓ |
| Transport | `RdmaSend`, `RdmaRecv`, `RdmaRead`, `RdmaWrite` | ✓ | ✓ | ✓ |
| Storage | `StorageRead`, `StorageWrite`, PyTorch | ✓ | ✓ | ✓ |

`ra` is the NPU-attached host CANN RA path; `aiv` is an NPU vector-core
operator; `aicpu` is a standard-CP1 CPU-kernel operator.

See [design](docs/design.md) for lifecycle, completion, and API details.

## Build

Building and running NDS requires a real Linux target with an Ascend NPU, a
matching CANN installation, and configured NPU and CPU RoCE devices.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## Repository Layout

```text
src/        Reusable client, server, common, and Torch wrapper libraries
apps/       Complete storage and Torch applications
examples/   Paired lower-layer client/server examples
benchmarks/ Opt-in performance workloads
tests/      Unit, integration, and opt-in hardware validation
docs/       Design, development, roadmap, and reference guides
```

## Documentation

- [Design](docs/design.md): architecture, lifecycle, wire boundary, completion,
  runtime ABI, and execution modes.
- [Development](docs/development.md): C++ conventions, formatting, testing,
  hardware validation, and the PyTorch wrapper.
- [Roadmap](docs/roadmap.md): delivered work and planned engineering tracks.
- [Reference basis](docs/references.md): source provenance and evidence rules.

# NPU Direct Storage (NDS)

NDS is a storage interoperability project for one Ascend NPU RNIC client and
one CPU-side RoCE server. The NPU submits storage commands; the CPU owns the
memory-backed namespace and data movement through `libibverbs`.

> The API and implementation are under active development and may change without notice.

## Basis

Its design, lifecycle, and ABI understanding are based on public open-source
projects ([HCOMM](https://gitcode.com/cann/hcomm),
[HCCL](https://gitcode.com/cann/hccl), and
[rdma-core](https://github.com/linux-rdma/rdma-core)) and Ascend documentation.
The installed CANN libraries on the target remain authoritative at runtime. See
[reference basis](docs/references.md).

## API Matrix

NDS supports three backend modes across three API layers:

| Layer | APIs | `ra` | `aiv` | `aicpu` |
|---|---|---|---|---|
| Verbs | `PostSend`, `PostRecv`, optional `PollCq(is_send_cq)` | ✓ | ✓ | ✓ |
| Transport | `RdmaSend`, `RdmaRecv`, `RdmaRead`, `RdmaWrite` | ✓ | ✓ | ✓ |
| Storage | `StorageRead`, `StorageWrite`, batch Read/Write, PyTorch | ✓ | ✓ | ✓ |

`ra` is the NPU-attached host CANN RA path; `aiv` is an NPU vector-core
operator; `aicpu` is a standard-CP1 CPU-kernel operator.

See [design](docs/design.md) for lifecycle, completion, and API details.

## Build

Building and running NDS requires a real Linux target with an Ascend NPU, a
matching CANN installation, and configured NPU and CPU RoCE devices.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DNDS_CANN_ROOT=<cann-root>
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## Repository Layout

```text
src/        Reusable client, server, common, and Torch wrapper libraries
examples/   Storage, verbs, and transport client/server examples
benchmarks/ Opt-in performance workloads
tests/      Unit, integration, and opt-in layer E2E validation
docs/       Design, development, roadmap, and reference guides
```

## Documentation

- [Design](docs/design.md): architecture, lifecycle, wire boundary, completion,
  runtime ABI, and backend modes.
- [Development](docs/development.md): C++ conventions, formatting, testing,
  hardware validation, and the PyTorch wrapper.
- [Roadmap](docs/roadmap.md): delivered work and planned engineering tracks.
- [Reference basis](docs/references.md): source provenance and evidence rules.

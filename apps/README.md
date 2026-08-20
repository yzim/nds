# NDS Applications

`apps/` contains complete NDS programs. An application composes reusable
libraries from `src/`, owns its command-line interface and workload
verification, and represents a supported end-to-end workflow.

| Program | Role | Layer |
|---|---|---|
| `nds_client` | NPU-side storage application | Storage |
| `nds_server` | CPU-side memory-backed storage application | Storage |
| `nds_torch.py` | Tensor-facing storage application | Storage through `_nds_torch` |

`nds_client` and `nds_server` form the native storage application pair. They
exercise the composed storage -> transport -> verbs path. The Python program
imports the reusable `_nds_torch` extension built from `src/torch`; it must not
reimplement runtime, transport, or storage ownership.

Do not add verbs or transport probes here. Those are lower-layer demonstrations
and belong in `examples/` as dedicated client/server pairs. The storage server
has no mode that reinterprets it as a generic receive probe.

All builds and hardware runs must occur only on the approved aarch64 CANN
target. See the repository [README](../README.md),
[design guide](../docs/design.md), and
[development guide](../docs/development.md).

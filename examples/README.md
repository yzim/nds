# NDS Layer Examples

`examples/` contains paired client/server programs for each NDS API layer.
Each pair demonstrates one layer and its wire contract. Storage builds with the
normal project targets; the lower-layer probes are opt-in hardware targets.

| Directory | Client target | Server target | Scope |
|---|---|---|---|
| `storage/` | `nds_storage_client` | `nds_storage_server` | NDS storage commands and namespace verification |
| `verbs/` | `nds_verbs_client` | `nds_verbs_server` | NPU PostSend, PostRecv, PollCq, and AIV batch posting |
| `transport/` | `nds_transport_client` | `nds_transport_server` | NPU RdmaSend, RdmaRecv, RdmaRead, and RdmaWrite peer workflows |

Configure the verbs and transport pairs with `-DNDS_BUILD_HARDWARE_PROBES=ON`.
All example binaries are emitted in `build/bin/` and must be run only on the
approved aarch64 CANN target with one bounded experiment.

Backend selection (`ra`, `aiv`, or `aicpu`) is a backend-mode axis within a
client example where the selected layer supports it. It does not create another
example layer or change the server into a storage application.

`storage/` is the complete NDS storage workflow. Its native pair is
`nds_storage_client`/`nds_storage_server`; `torch.py` is its tensor-facing
frontend. The reusable Torch extension remains in `src/torch`.

Keep future layer behavior in the matching pair and update its E2E runner
alongside the executable. See [development](../docs/development.md) for the
hardware-validation rules.

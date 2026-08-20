# NDS Layer Examples

`examples/` contains paired client/server programs for lower NDS API layers.
Each pair demonstrates one layer and its wire contract without adding storage
protocol semantics. The programs are opt-in hardware targets.

| Directory | Client target | Server target | Scope |
|---|---|---|---|
| `verbs/` | `nds_verbs_client` | `nds_verbs_server` | NPU verbs post/local completion and CPU receive/CQ |
| `transport/` | `nds_transport_client` | `nds_transport_server` | NPU bootstrap/transport operation and server exchange/receive |

Configure them with `-DNDS_BUILD_HARDWARE_PROBES=ON`. They are emitted in
`build/bin/` and must be run only on the approved aarch64 CANN target with one
bounded experiment.

Backend selection (`ra`, `aiv`, or `aicpu`) is an execution-mode axis within a
client example where the selected layer supports it. It does not create another
example layer or change the server into a storage application.

Storage is deliberately not an example: it is the complete NDS application and
lives in `apps/` as the native `nds_client`/`nds_server` pair and the
`nds_torch.py` frontend. The reusable Torch extension remains in `src/torch`.

Keep future lower-layer behavior in the matching pair and update its E2E runner
alongside the executable. See [testing](../docs/testing.md) for the
hardware-validation rules.

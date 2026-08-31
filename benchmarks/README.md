# NDS Benchmarks

This directory contains target-only performance workloads. It is excluded
from normal builds and CI; configure it with `-DNDS_BUILD_BENCHMARKS=ON`.

Benchmarks must separate session/bootstrap cost from steady-state operation
cost, use bounded warmups and iterations, and record the backend, payload
size, host topology, CANN version, and commit under ignored `.local/` state.

Storage benchmarks should reuse one persistent NDS session. Reconnecting for
each operation measures setup cost rather than storage performance.

`nds_verbs_benchmark_client` and `nds_verbs_benchmark_server` run the direct
Send verbs workload from `examples/verbs`. The client accepts `--backend-mode ra`,
`--backend-mode aiv`, or `--backend-mode aicpu`, plus `--backend-artifact-path`. The selected
backend interprets the artifact as its kernel object, package descriptor, or
NDS-built RA backend shared library containing verbs, transport, and storage.
Each endpoint owns one QP and one MR. The common TCP socket exchanges versioned
QP records; the workload uses the selected client backend and CPU
`VerbsBackend` directly, without NDS transport or storage classes.

# NDS Benchmarks

This directory contains target-only performance workloads. It is excluded
from normal builds and CI; configure it with `-DNDS_BUILD_BENCHMARKS=ON`.

Benchmarks must separate session/bootstrap cost from steady-state operation
cost, use bounded warmups and iterations, and record the backend, payload
size, host topology, CANN version, and commit under ignored `.local/` state.

Storage benchmarks should reuse one persistent NDS session. Reconnecting for
each operation measures setup cost rather than storage performance.

`nds_verbs_benchmark_client` accepts `--backend ra`, `--backend aiv`, or
`--backend aicpu` and measures direct verbs RDMA Read or Write against the
paired CPU DRAM server. The timed interval excludes runtime, QP, MR, and
kernel-package setup. `--in-flight` posts one window, signals its final WR,
and polls that final send CQE before reusing the window. RA prepares the window
then rings one runtime doorbell; AIV uses its contiguous batch post and one
doorbell; AICPU is currently limited to `--in-flight 1` because its linked
provider-post implementation is not yet part of this app. This is a
backend-only benchmark, not end-to-end storage performance.

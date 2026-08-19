# NDS Benchmarks

This directory contains target-only performance workloads. It is excluded
from normal builds and CI; configure it with `-DNDS_BUILD_BENCHMARKS=ON`.

Benchmarks must separate session/bootstrap cost from steady-state operation
cost, use bounded warmups and iterations, and record the backend, payload
size, host topology, CANN version, and commit under ignored `.local/` state.

Storage benchmarks should reuse one persistent NDS session. Reconnecting for
each operation measures setup cost rather than storage performance.

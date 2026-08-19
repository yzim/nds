# PyTorch Wrappers

NDS provides an optional `_nds_torch` extension under `src/torch` and a
user-facing `apps/nds_torch.py` program for tensor-facing storage operations.
The extension keeps runtime, endpoint, QP, registered-memory, and
storage-session ownership in C++.

The extension is built only on the target host. It requires a PyTorch and
`torch_npu` pair compatible with the installed CANN runtime.

```sh
source <cann-root>/set_env.sh
cmake -S . -B build-torch \
  -DNDS_BUILD_TORCH_WRAPPERS=ON \
  -DPython3_EXECUTABLE=<python3.10> \
  -DCMAKE_PREFIX_PATH=<torch-cmake-prefix> \
  -DNDS_CANN_ROOT=<cann-root> \
  -DNDS_BUILD_AIV_KERNEL=ON \
  -DNDS_BUILD_AICPU_KERNEL=ON
cmake --build build-torch --target _nds_torch --parallel
```

Import the resulting module by adding `build/bin` to `PYTHONPATH`; run the
user-facing program from `apps/nds_torch.py`.
The process must run with the CANN environment configured; access to the NPU
may require the same `sudo -n` context as other NDS hardware programs.

```python
import torch
import torch_npu
import _nds_torch

session = _nds_torch.Session("192.168.100.100:18615", backend="ra")

payload = torch.full((4096,), 0x5A, dtype=torch.uint8)
session.write(payload, 0)
```

`Session` exposes `read_`, `write`, and a `capacity` property. It establishes
one runtime, connected transport, and storage bootstrap during construction;
calls execute serially on that same connection.

Session accepts the NDS execution backend selected at construction: `ra`,
`aiv`, or `aicpu`.

All wrappers currently accept nonempty, contiguous CPU tensors. They copy data
through NDS-owned NPU allocations before registration. Direct `torch_npu`
tensor registration is intentionally not exposed yet: it needs a borrowed
allocation lifetime and stream-synchronization contract with the PyTorch NPU
allocator.

`Session` adopts the active `torch_npu` context and current NPU device. The CANN
DSMI query used by `hccn_tool` resolves the matching RoCE IPv4 address from that
physical device; the session does not accept an NPU IP address, AscendCL/runtime
library paths, or
device IDs. Import `torch_npu` and select its device before creating a session.
Vendor libraries are resolved through the CANN environment.

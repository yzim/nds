# PyTorch Wrappers

NDS provides an optional `nds_torch` Python extension for tensor-facing
storage operations. It keeps runtime, endpoint, QP, registered-memory, and
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
cmake --build build-torch --target nds_torch --parallel
```

Import the resulting module by adding the build directory to `PYTHONPATH`.
The process must run with the CANN environment configured; access to the NPU
may require the same `sudo -n` context as other NDS hardware programs.

```python
import nds_torch
import torch
import torch_npu

session = nds_torch.Session("192.168.100.100:18615", backend="ra")

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

`Session` adopts the active `torch_npu` context and current NPU device. It does
not accept AscendCL or runtime library paths, device IDs, or an NPU IP address.
Import `torch_npu` and select its device before creating a session. Vendor
libraries are resolved through the CANN environment.

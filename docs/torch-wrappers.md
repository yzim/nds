# PyTorch Wrappers

NDS provides an optional `nds_torch` Python extension. It wraps the three NDS
operator layers while keeping runtime, endpoint, QP, registered-memory, and
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

storage = session.storage()
payload = torch.full((4096,), 0x5A, dtype=torch.uint8)
storage.write(payload, 0)
```

## Layer API

`Session.verbs()` returns a `Verbs` wrapper with `post_send`, `post_receive`,
`poll_send`, and `poll_receive`. `post_send` returns the RA doorbell index and
doorbell value; it intentionally does not ring the doorbell.

`Session.transport()` returns a `Transport` wrapper with `send`, which posts
and rings the RA doorbell.

`Session.storage()` returns a `Storage` wrapper with `read_`, `write`, and a
`capacity` property. The current NDS storage protocol supports one command per
connection, so each `Session` may create one storage wrapper and submit one
storage operation.

Storage accepts the NDS execution backend selected when the session is
created: `ra`, `aiv`, or `aicpu`. Raw verbs and transport wrappers currently
require `ra`, because NDS has no host-side completion and doorbell interface
for raw AIV or AICPU operations.

All wrappers currently accept nonempty, contiguous CPU tensors. They copy data
through NDS-owned NPU allocations before registration. Direct `torch_npu`
tensor registration is intentionally not exposed yet: it needs a borrowed
allocation lifetime and stream-synchronization contract with the PyTorch NPU
allocator.

`Session` adopts the active `torch_npu` context and current NPU device. It does
not accept AscendCL or runtime library paths, device IDs, or an NPU IP address.
Import `torch_npu` and select its device before creating a session. Vendor
libraries are resolved through the CANN environment.

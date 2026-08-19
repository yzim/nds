#include "nds/device_operations.h"
#include "nds/device_storage.h"

#include <stddef.h>

_Static_assert(sizeof(nds_device_qp) == 240U, "device QP ABI changed");
_Static_assert(sizeof(nds_device_transport) == 248U, "device transport ABI changed");
_Static_assert(sizeof(nds_device_operation_request) == 312U, "device operation ABI changed");
_Static_assert(sizeof(nds_device_storage_request) == 360U, "device storage ABI changed");
_Static_assert(offsetof(nds_device_operation_request, transport) == 16U, "device operation transport offset changed");

int main(void) {
    nds_device_operation_request request = {0};
    request.abi_version = NDS_DEVICE_OPERATIONS_ABI_VERSION;
    request.size = sizeof(request);
    return request.size == 312U ? 0 : 1;
}

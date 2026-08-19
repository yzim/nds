#include "nds/device_operations.h"
#include "nds/device_storage.h"

int main(void) {
    nds_device_operation_request request = {0};
    request.abi_version = NDS_DEVICE_OPERATIONS_ABI_VERSION;
    request.size = sizeof(request);
    return request.abi_version == NDS_DEVICE_OPERATIONS_ABI_VERSION && request.size == sizeof(request) ? 0 : 1;
}

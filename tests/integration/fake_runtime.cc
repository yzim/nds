#include <stdint.h>

extern "C" {

int rtSetDevice(int32_t logical_device_id) {
    return logical_device_id == 0 ? 0 : -33;
}

int rtOpenNetService(const void *args) {
    return args == 0 ? -34 : 0;
}

int rtCloseNetService(void) {
    return 0;
}

int rtRDMADBSend(uint32_t db_index, uint64_t db_info, void *stream) {
    if (db_index != 0x1234U || stream != 0) {
        return -35;
    }
    return db_info == UINT64_C(0xdead) ? -77 : 0;
}
}

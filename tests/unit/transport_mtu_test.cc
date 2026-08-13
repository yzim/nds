#include "nds/transport.h"

#include <stdio.h>

static int expect(int condition, const char *message) {
    if (condition == 0) {
        (void)fprintf(stderr, "FAIL: %s\n", message);
        return -1;
    }
    return 0;
}

int main(void) {
    static const uint32_t supported[] = {256U, 512U, 1024U, 2048U, 4096U};
    size_t index;

    for (index = 0U; index < sizeof(supported) / sizeof(supported[0]); ++index) {
        if (expect(nds_transport_mtu_is_supported(supported[index]) != 0, "supported MTU accepted") != 0 ||
            expect(nds_transport_mtu_select(supported[index], 1024U) == supported[index],
                   "CPU QP retains local active MTU") != 0) {
            return 1;
        }
    }
    if (expect(nds_transport_mtu_is_supported(0U) == 0, "zero MTU rejected") != 0 ||
        expect(nds_transport_mtu_is_supported(1536U) == 0, "nonstandard MTU rejected") != 0 ||
        expect(nds_transport_mtu_select(1536U, 4096U) == 0U, "invalid local MTU has no policy result") != 0 ||
        /* Regression: a peer's smaller record must never clamp the CPU QP. */
        expect(nds_transport_mtu_select(4096U, 1024U) == 4096U,
               "local 4096 policy remains independent of a peer 1024 record") != 0) {
        return 1;
    }
    (void)puts("transport MTU policy tests passed");
    return 0;
}

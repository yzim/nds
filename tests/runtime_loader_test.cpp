#include "nds/runtime_loader.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    nds_runtime_api api = {};
    nds_rt_proc_ext_param parameter = {"--hdcType=18", 12U};
    nds_rt_net_service_open_args args = {&parameter, 1U};

    if (argc != 2) {
        (void)fprintf(stderr, "usage: %s <fake-runtime-library>\n", argv[0]);
        return 1;
    }
    if (nds_runtime_open(&api, "") == 0 ||
        strstr(nds_runtime_error(&api), "non-empty library path") == NULL) {
        (void)fprintf(stderr, "runtime loader did not reject an empty path: %s\n", nds_runtime_error(&api));
        return 1;
    }
    if (nds_runtime_open(&api, argv[1]) != 0) {
        (void)fprintf(stderr, "runtime loader could not open fake runtime: %s\n", nds_runtime_error(&api));
        return 1;
    }
    if (api.rdma_db_send == NULL || api.set_device == NULL || api.open_net_service == NULL ||
        api.close_net_service == NULL) {
        (void)fprintf(stderr, "runtime loader returned an incomplete ABI\n");
        nds_runtime_close(&api);
        return 1;
    }
    if (api.set_device(0) != 0 || api.open_net_service(&args) != 0 ||
        api.rdma_db_send(0x1234U, UINT64_C(0x10000006a), NULL) != 0 || api.close_net_service() != 0) {
        (void)fprintf(stderr, "runtime loader did not dispatch successful fake-runtime calls\n");
        nds_runtime_close(&api);
        return 1;
    }
    if (api.rdma_db_send(0x1234U, UINT64_C(0xdead), NULL) != -77) {
        (void)fprintf(stderr, "runtime loader did not expose rtRDMADBSend failure\n");
        nds_runtime_close(&api);
        return 1;
    }
    nds_runtime_close(&api);
    if (api.library != NULL || api.rdma_db_send != NULL) {
        (void)fprintf(stderr, "runtime loader close did not clear state\n");
        return 1;
    }
    (void)puts("Runtime loader doorbell ABI tests passed");
    return 0;
}

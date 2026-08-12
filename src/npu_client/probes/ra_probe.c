#include "nds/ra_loader.h"

#include <stdio.h>
#include <stdlib.h>

static void usage(const char *program)
{
    (void)fprintf(stderr, "usage: %s <path-to-libra.so>\n", program);
}

int main(int argc, char **argv)
{
    nds_ra_api api = {0};

    if (argc != 2) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }
    if (nds_ra_open(&api, argv[1]) != 0) {
        (void)fprintf(stderr, "HCCP/RA loader probe failed: %s\n", nds_ra_error(&api));
        return EXIT_FAILURE;
    }
    (void)printf("HCCP/RA loader probe succeeded: %s\n", argv[1]);
    (void)printf("Resolved lifecycle and operation symbols: "
                 "RaInit, RaDeinit, RaRdevInit, RaRdevDeinit, RaQpCreate, "
                 "RaQpConnectAsync, RaTypicalQpCreate, RaTypicalQpModify, "
                 "RaQpDestroy, RaGetQpAttr, RaRegisterMr, RaDeregisterMr\n");
    nds_ra_close(&api);
    return EXIT_SUCCESS;
}

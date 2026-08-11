#include "nds/acl_loader.h"
#include "nds/hcomm_loader.h"
#include "nds/ra_loader.h"
#include "nds/tsd_loader.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *program)
{
    (void)fprintf(stderr,
                  "usage: %s <libascendcl.so> <libtsdclient.so> <libhcomm.so> <libra.so>\n"
                  "       <rank-table.json> <npu-rnic-ipv4> <logical-device-id>\n"
                  "       [--npu-physical-id N] [--rank-size N] [--identify NAME] [--preopen-tsd] --execute\n",
                  program);
}

static int parse_uint(const char *text, unsigned int *value)
{
    char *end = NULL;
    const unsigned long parsed = strtoul(text, &end, 10);

    if (end == text || *end != '\0' || parsed > UINT32_MAX) {
        return -1;
    }
    *value = (unsigned int)parsed;
    return 0;
}

int main(int argc, char **argv)
{
    nds_acl_api acl_api = {0};
    nds_tsd_api tsd_api = {0};
    nds_hcomm_api hcomm_api = {0};
    nds_ra_api ra_api = {0};
    nds_ra_rdev rdev = {0};
    nds_ra_typical_qp local_qp = {0};
    const char *identify = "nds-hcomm-ra-rdev";
    unsigned int logical_device_id;
    unsigned int physical_device_id = NDS_RA_PHY_ID_NPU0;
    unsigned int rank_size = 2;
    int preopen_tsd = 0;
    int execute = 0;
    int acl_initialized = 0;
    int tsd_opened = 0;
    int hcomm_initialized = 0;
    void *rdev_handle = NULL;
    void *qp_handle = NULL;
    int capability = 0;
    int result;
    uint32_t tsd_result;
    int index;

    if (argc < 8 || parse_uint(argv[7], &logical_device_id) != 0) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }
    for (index = 8; index < argc; ++index) {
        if (strcmp(argv[index], "--npu-physical-id") == 0) {
            if (++index >= argc || parse_uint(argv[index], &physical_device_id) != 0) {
                usage(argv[0]);
                return EXIT_FAILURE;
            }
            continue;
        }
        if (strcmp(argv[index], "--rank-size") == 0) {
            if (++index >= argc || parse_uint(argv[index], &rank_size) != 0 || rank_size < 2) {
                usage(argv[0]);
                return EXIT_FAILURE;
            }
            continue;
        }
        if (strcmp(argv[index], "--identify") == 0) {
            if (++index >= argc || argv[index][0] == '\0') {
                usage(argv[0]);
                return EXIT_FAILURE;
            }
            identify = argv[index];
            continue;
        }
        if (strcmp(argv[index], "--preopen-tsd") == 0) {
            preopen_tsd = 1;
            continue;
        }
        if (strcmp(argv[index], "--execute") == 0) {
            execute = 1;
            continue;
        }
        usage(argv[0]);
        return EXIT_FAILURE;
    }
    if (execute == 0) {
        (void)fprintf(stderr, "refusing hardware lifecycle: pass --execute after rank-table validation\n");
        return EXIT_FAILURE;
    }

    if (nds_acl_open(&acl_api, argv[1]) != 0) {
        (void)fprintf(stderr, "AscendCL loader failed: %s\n", nds_acl_error(&acl_api));
        goto fail;
    }
    result = acl_api.init(NULL);
    if (result != 0) {
        (void)fprintf(stderr, "aclInit(NULL) failed: %d\n", result);
        goto fail;
    }
    acl_initialized = 1;
    result = acl_api.set_device((int32_t)logical_device_id);
    if (result != 0) {
        (void)fprintf(stderr, "aclrtSetDevice(%u) failed: %d\n", logical_device_id, result);
        goto fail;
    }

    if (nds_tsd_open_library(&tsd_api, argv[2]) != 0) {
        (void)fprintf(stderr, "TSD loader failed: %s\n", nds_tsd_error(&tsd_api));
        goto fail;
    }
    tsd_result = tsd_api.capability_get(logical_device_id, NDS_TSD_CAPABILITY_MULTIPLE_HCCP,
                                        (uint64_t)(uintptr_t)&capability);
    if (tsd_result != 0U || capability == 0) {
        (void)fprintf(stderr, "process-granular HCCP capability unavailable: result=%u, supported=%d\n",
                      tsd_result, capability);
        goto fail;
    }
    if (preopen_tsd != 0) {
        tsd_result = tsd_api.open(logical_device_id, rank_size);
        if (tsd_result != 0U) {
            (void)fprintf(stderr, "TsdOpen(%u, %u) failed: %u\n", logical_device_id, rank_size, tsd_result);
            goto fail;
        }
        tsd_opened = 1;
    }

    if (nds_hcomm_open(&hcomm_api, argv[3]) != 0) {
        (void)fprintf(stderr, "HCOMM loader failed: %s\n", nds_hcomm_error(&hcomm_api));
        goto fail;
    }
    result = hcomm_api.init_by_file(argv[5], identify);
    if (result != 0) {
        (void)fprintf(stderr, "HcomInitByFile failed: %d\n", result);
        goto fail;
    }
    hcomm_initialized = 1;

    if (nds_ra_open(&ra_api, argv[4]) != 0) {
        (void)fprintf(stderr, "RA loader failed: %s\n", nds_ra_error(&ra_api));
        goto fail;
    }
    rdev.phy_id = physical_device_id;
    rdev.family = AF_INET;
    if (inet_pton(AF_INET, argv[6], &rdev.local_ip.ipv4) != 1) {
        (void)fprintf(stderr, "invalid NPU RNIC IPv4 address: %s\n", argv[6]);
        goto fail;
    }

    /* HCOMM owns RA initialization. NDS starts at the rdev/QP boundary. */
    result = ra_api.ra_rdev_init(NDS_RA_NETWORK_OFFLINE, NDS_RA_NOTIFY_NO_USE, rdev, &rdev_handle);
    if (result != 0 || rdev_handle == NULL) {
        (void)fprintf(stderr, "RaRdevInit failed: %d\n", result);
        goto fail;
    }
    result = ra_api.ra_typical_qp_create(rdev_handle, NDS_RA_QP_FLAG_RC, NDS_RA_QP_MODE_OPBASE,
                                         &local_qp, &qp_handle);
    if (result != 0 || qp_handle == NULL) {
        (void)fprintf(stderr, "RaTypicalQpCreate failed: %d\n", result);
        goto fail;
    }
    (void)printf("HCOMM-owned RA context created an NPU rdev and local RC QP: qpn=%u psn=%u gid-index=%u\n",
                 local_qp.qpn, local_qp.psn, local_qp.gid_index);

    result = ra_api.ra_qp_destroy(qp_handle);
    if (result != 0) {
        (void)fprintf(stderr, "RaQpDestroy failed: %d\n", result);
        qp_handle = NULL;
        goto fail;
    }
    qp_handle = NULL;
    result = ra_api.ra_rdev_deinit(rdev_handle, NDS_RA_NOTIFY_NO_USE);
    if (result != 0) {
        (void)fprintf(stderr, "RaRdevDeinit failed: %d\n", result);
        rdev_handle = NULL;
        goto fail;
    }
    rdev_handle = NULL;
    nds_ra_close(&ra_api);
    result = hcomm_api.destroy();
    if (result != 0) {
        (void)fprintf(stderr, "HcomDestroy failed: %d\n", result);
        hcomm_initialized = 0;
        goto fail;
    }
    hcomm_initialized = 0;
    nds_hcomm_close(&hcomm_api);
    if (tsd_opened != 0) {
        tsd_result = tsd_api.close(logical_device_id);
        if (tsd_result != 0U) {
            (void)fprintf(stderr, "TsdClose(%u) failed: %u\n", logical_device_id, tsd_result);
            tsd_opened = 0;
            goto fail;
        }
        tsd_opened = 0;
    }
    nds_tsd_close_library(&tsd_api);
    result = acl_api.finalize();
    if (result != 0) {
        (void)fprintf(stderr, "aclFinalize failed: %d\n", result);
        acl_initialized = 0;
        goto fail;
    }
    acl_initialized = 0;
    nds_acl_close(&acl_api);
    (void)printf("HCOMM-to-RA rdev/QP lifecycle completed successfully.\n");
    return EXIT_SUCCESS;

fail:
    if (qp_handle != NULL) {
        (void)ra_api.ra_qp_destroy(qp_handle);
    }
    if (rdev_handle != NULL) {
        (void)ra_api.ra_rdev_deinit(rdev_handle, NDS_RA_NOTIFY_NO_USE);
    }
    nds_ra_close(&ra_api);
    if (hcomm_initialized != 0) {
        (void)hcomm_api.destroy();
    }
    nds_hcomm_close(&hcomm_api);
    if (tsd_opened != 0) {
        (void)tsd_api.close(logical_device_id);
    }
    nds_tsd_close_library(&tsd_api);
    if (acl_initialized != 0) {
        (void)acl_api.finalize();
    }
    nds_acl_close(&acl_api);
    return EXIT_FAILURE;
}

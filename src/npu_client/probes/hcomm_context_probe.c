#include "nds/acl_loader.h"
#include "nds/hcomm_loader.h"
#include "nds/tsd_loader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void stage(const char *name)
{
    (void)fprintf(stderr, "[stage] %s\n", name);
    (void)fflush(stderr);
}

static void hold_for_ms(unsigned int milliseconds)
{
    const unsigned int seconds = (milliseconds + 999U) / 1000U;

    if (seconds != 0U) {
        (void)sleep(seconds);
    }
}

static void usage(const char *program)
{
    (void)fprintf(stderr,
                  "usage: %s <path-to-libascendcl.so> <path-to-libtsdclient.so> <path-to-libhcomm.so>\n"
                  "       <rank-table.json> <logical-device-id> [--identify NAME] [--rank-size N] [--preopen-tsd]\n"
                  "       [--hold-ms N] --execute\n",
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
    const char *identify = "nds-hcomm-bootstrap";
    unsigned int logical_device_id;
    unsigned int rank_size = 2;
    unsigned int hold_ms = 0;
    int preopen_tsd = 0;
    int execute = 0;
    int acl_initialized = 0;
    int tsd_opened = 0;
    int hcomm_initialized = 0;
    int capability = 0;
    int result;
    uint32_t tsd_result;
    int index;

    if (argc < 6 || parse_uint(argv[5], &logical_device_id) != 0) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }
    for (index = 6; index < argc; ++index) {
        if (strcmp(argv[index], "--identify") == 0) {
            if (++index >= argc || argv[index][0] == '\0') {
                usage(argv[0]);
                return EXIT_FAILURE;
            }
            identify = argv[index];
            continue;
        }
        if (strcmp(argv[index], "--rank-size") == 0) {
            if (++index >= argc || parse_uint(argv[index], &rank_size) != 0 || rank_size < 2) {
                usage(argv[0]);
                return EXIT_FAILURE;
            }
            continue;
        }
        if (strcmp(argv[index], "--preopen-tsd") == 0) {
            preopen_tsd = 1;
            continue;
        }
        if (strcmp(argv[index], "--hold-ms") == 0) {
            if (++index >= argc || parse_uint(argv[index], &hold_ms) != 0) {
                usage(argv[0]);
                return EXIT_FAILURE;
            }
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
        (void)fprintf(stderr, "refusing to touch CANN/HCOMM resources without --execute\n");
        return EXIT_FAILURE;
    }

    stage("acl-loader-open");
    if (nds_acl_open(&acl_api, argv[1]) != 0) {
        (void)fprintf(stderr, "AscendCL loader failed: %s\n", nds_acl_error(&acl_api));
        goto fail;
    }
    stage("aclInit-begin");
    result = acl_api.init(NULL);
    if (result != 0) {
        (void)fprintf(stderr, "aclInit(NULL) failed: %d\n", result);
        goto fail;
    }
    acl_initialized = 1;
    stage("aclInit-complete");
    stage("aclrtSetDevice-begin");
    result = acl_api.set_device((int32_t)logical_device_id);
    if (result != 0) {
        (void)fprintf(stderr, "aclrtSetDevice(%u) failed: %d\n", logical_device_id, result);
        goto fail;
    }

    stage("aclrtSetDevice-complete");
    stage("tsd-loader-open");
    if (nds_tsd_open_library(&tsd_api, argv[2]) != 0) {
        (void)fprintf(stderr, "TSD loader failed: %s\n", nds_tsd_error(&tsd_api));
        goto fail;
    }
    stage("TsdCapabilityGet-begin");
    tsd_result = tsd_api.capability_get(logical_device_id, NDS_TSD_CAPABILITY_MULTIPLE_HCCP,
                                        (uint64_t)(uintptr_t)&capability);
    if (tsd_result != 0U) {
        (void)fprintf(stderr, "TsdCapabilityGet(MULTIPLE_HCCP) failed: %u\n", tsd_result);
        goto fail;
    }
    if (capability == 0) {
        (void)fprintf(stderr, "TSD does not report process-granular HCCP support.\n");
        goto fail;
    }
    stage("TsdCapabilityGet-complete");
    (void)printf("ACL device context and process-granular HCCP capability are ready.\n");

    if (preopen_tsd != 0) {
        stage("TsdOpen-begin");
        tsd_result = tsd_api.open(logical_device_id, rank_size);
        if (tsd_result != 0U) {
            (void)fprintf(stderr, "TsdOpen(%u, %u) failed: %u\n", logical_device_id, rank_size, tsd_result);
            goto fail;
        }
        tsd_opened = 1;
        stage("TsdOpen-complete");
        (void)printf("Opened TSD communication context for %u ranks.\n", rank_size);
    }

    stage("hcomm-loader-open");
    if (nds_hcomm_open(&hcomm_api, argv[3]) != 0) {
        (void)fprintf(stderr, "HCOMM loader failed: %s\n", nds_hcomm_error(&hcomm_api));
        goto fail;
    }
    stage("HcomInitByFile-begin");
    result = hcomm_api.init_by_file(argv[4], identify);
    if (result != 0) {
        (void)fprintf(stderr, "HcomInitByFile(%s, %s) failed: %d\n", argv[4], identify, result);
        goto fail;
    }
    hcomm_initialized = 1;
    stage("HcomInitByFile-complete");
    (void)printf("HCOMM communicator initialized. It now owns its HCCL/HCCP/RA network resources.\n");
    if (hold_ms != 0U) {
        stage("hold-after-HcomInitByFile");
        hold_for_ms(hold_ms);
        stage("hold-complete");
    }

    stage("HcomDestroy-begin");
    result = hcomm_api.destroy();
    if (result != 0) {
        (void)fprintf(stderr, "HcomDestroy failed: %d\n", result);
        hcomm_initialized = 0;
        goto fail;
    }
    hcomm_initialized = 0;
    stage("HcomDestroy-complete");
    stage("hcomm-loader-close");
    nds_hcomm_close(&hcomm_api);
    stage("hcomm-loader-closed");
    if (tsd_opened != 0) {
        stage("TsdClose-begin");
        tsd_result = tsd_api.close(logical_device_id);
        if (tsd_result != 0U) {
            (void)fprintf(stderr, "TsdClose(%u) failed: %u\n", logical_device_id, tsd_result);
            tsd_opened = 0;
            goto fail;
        }
        tsd_opened = 0;
        stage("TsdClose-complete");
    }
    stage("tsd-loader-close");
    nds_tsd_close_library(&tsd_api);
    stage("tsd-loader-closed");
    stage("aclFinalize-begin");
    result = acl_api.finalize();
    if (result != 0) {
        (void)fprintf(stderr, "aclFinalize failed: %d\n", result);
        acl_initialized = 0;
        goto fail;
    }
    acl_initialized = 0;
    stage("aclFinalize-complete");
    stage("acl-loader-close");
    nds_acl_close(&acl_api);
    stage("acl-loader-closed");
    (void)printf("HCOMM context lifecycle completed successfully.\n");
    return EXIT_SUCCESS;

fail:
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

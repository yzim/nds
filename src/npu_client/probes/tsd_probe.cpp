#include "nds/acl_loader.h"
#include "nds/tsd_loader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *program)
{
    (void)fprintf(stderr,
                  "usage: %s <path-to-libascendcl.so> <path-to-libtsdclient.so> <logical-device-id>\n"
                  "       [--rank-size N] [--open]\n",
                  program);
}

static int parse_uint(const char *text, unsigned int *value)
{
    char *end = nullptr;
    const unsigned long parsed = strtoul(text, &end, 10);

    if (end == text || *end != '\0' || parsed > UINT32_MAX) {
        return -1;
    }
    *value = (unsigned int)parsed;
    return 0;
}

int main(int argc, char **argv)
{
    nds_acl_api acl_api = {};
    nds_tsd_api tsd_api = {};
    unsigned int logical_device_id;
    unsigned int rank_size = 2;
    int acl_initialized = 0;
    int tsd_opened = 0;
    int should_open = 0;
    int capability = 0;
    int result;
    uint32_t tsd_result;
    int index;

    if (argc < 4 || parse_uint(argv[3], &logical_device_id) != 0) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }
    for (index = 4; index < argc; ++index) {
        if (strcmp(argv[index], "--rank-size") == 0) {
            if (++index >= argc || parse_uint(argv[index], &rank_size) != 0 || rank_size == 0) {
                usage(argv[0]);
                return EXIT_FAILURE;
            }
            continue;
        }
        if (strcmp(argv[index], "--open") == 0) {
            should_open = 1;
            continue;
        }
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    if (nds_acl_open(&acl_api, argv[1]) != 0) {
        (void)fprintf(stderr, "AscendCL loader failed: %s\n", nds_acl_error(&acl_api));
        goto fail;
    }
    result = acl_api.init(nullptr);
    if (result != 0) {
        (void)fprintf(stderr, "aclInit(nullptr) failed: %d\n", result);
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
    if (tsd_result != 0U) {
        (void)fprintf(stderr, "TsdCapabilityGet(MULTIPLE_HCCP) failed: %u\n", tsd_result);
        goto fail;
    }
    (void)printf("TSD reports process-granular HCCP support: %s\n", capability != 0 ? "yes" : "no");

    if (should_open == 0) {
        (void)printf("Capability probe completed without opening TSD resources.\n");
        nds_tsd_close_library(&tsd_api);
        result = acl_api.finalize();
        if (result != 0) {
            (void)fprintf(stderr, "aclFinalize failed: %d\n", result);
            acl_initialized = 0;
            goto fail;
        }
        acl_initialized = 0;
        nds_acl_close(&acl_api);
        return EXIT_SUCCESS;
    }

    tsd_result = tsd_api.open(logical_device_id, rank_size);
    if (tsd_result != 0U) {
        (void)fprintf(stderr, "TsdOpen(%u, %u) failed: %u\n", logical_device_id, rank_size, tsd_result);
        goto fail;
    }
    tsd_opened = 1;
    (void)printf("TsdOpen(%u, %u) succeeded.\n", logical_device_id, rank_size);

    tsd_result = tsd_api.close(logical_device_id);
    if (tsd_result != 0U) {
        (void)fprintf(stderr, "TsdClose(%u) failed: %u\n", logical_device_id, tsd_result);
        tsd_opened = 0;
        goto fail;
    }
    tsd_opened = 0;
    nds_tsd_close_library(&tsd_api);
    result = acl_api.finalize();
    if (result != 0) {
        (void)fprintf(stderr, "aclFinalize failed: %d\n", result);
        acl_initialized = 0;
        goto fail;
    }
    acl_initialized = 0;
    nds_acl_close(&acl_api);
    (void)printf("TSD open/close probe completed successfully.\n");
    return EXIT_SUCCESS;

fail:
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

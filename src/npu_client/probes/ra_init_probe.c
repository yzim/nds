#include "nds/ra_loader.h"
#include "nds/runtime_loader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *program)
{
    (void)fprintf(stderr,
                  "usage: %s <path-to-libra.so> [npu-physical-id] [--hdc-type 6|18]\n"
                  "       [--bootstrap-net-service <path-to-libruntime.so>] [--logical-device-id N]\n",
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
    nds_ra_api api = {0};
    nds_runtime_api runtime_api = {0};
    nds_ra_init_config config = {
        .phy_id = NDS_RA_PHY_ID_NPU0,
        .nic_position = NDS_RA_NETWORK_OFFLINE,
        .hdc_type = NDS_RA_HDC_SERVICE_TYPE_RDMA,
        .enable_hdc_async = false,
    };
    const char *runtime_library_path = NULL;
    unsigned int logical_device_id = NDS_RA_PHY_ID_NPU0;
    int runtime_service_open = 0;
    int result;
    int index;

    if (argc < 2) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }
    for (index = 2; index < argc; ++index) {
        unsigned int value;

        if (strcmp(argv[index], "--hdc-type") == 0) {
            if (++index >= argc || parse_uint(argv[index], &value) != 0 ||
                (value != NDS_RA_HDC_SERVICE_TYPE_RDMA && value != NDS_RA_HDC_SERVICE_TYPE_RDMA_V2)) {
                usage(argv[0]);
                return EXIT_FAILURE;
            }
            config.hdc_type = (int)value;
            continue;
        }
        if (strcmp(argv[index], "--bootstrap-net-service") == 0) {
            if (++index >= argc || runtime_library_path != NULL) {
                usage(argv[0]);
                return EXIT_FAILURE;
            }
            runtime_library_path = argv[index];
            continue;
        }
        if (strcmp(argv[index], "--logical-device-id") == 0) {
            if (++index >= argc || parse_uint(argv[index], &logical_device_id) != 0) {
                usage(argv[0]);
                return EXIT_FAILURE;
            }
            continue;
        }
        if (parse_uint(argv[index], &value) != 0 || index != 2) {
            usage(argv[0]);
            return EXIT_FAILURE;
        }
        config.phy_id = value;
    }

    if (runtime_library_path != NULL) {
        const char hdc_type_arg[] = "--hdcType=18";
        nds_rt_proc_ext_param parameter = {
            .param_info = hdc_type_arg,
            .param_len = sizeof(hdc_type_arg) - 1U,
        };
        nds_rt_net_service_open_args open_args = {
            .ext_param_list = &parameter,
            .ext_param_count = 1U,
        };

        if (nds_runtime_open(&runtime_api, runtime_library_path) != 0) {
            (void)fprintf(stderr, "CANN runtime loader failed: %s\n", nds_runtime_error(&runtime_api));
            return EXIT_FAILURE;
        }
        result = runtime_api.set_device((int32_t)logical_device_id);
        if (result != 0) {
            (void)fprintf(stderr, "rtSetDevice(%u) failed: %d\n", logical_device_id, result);
            nds_runtime_close(&runtime_api);
            return EXIT_FAILURE;
        }
        (void)printf("Requesting device-side HCCP network service for logical device %u via rtOpenNetService.\n",
                     logical_device_id);
        result = runtime_api.open_net_service(&open_args);
        if (result != 0) {
            (void)fprintf(stderr, "rtOpenNetService failed: %d\n", result);
            nds_runtime_close(&runtime_api);
            return EXIT_FAILURE;
        }
        runtime_service_open = 1;
    }

    if (nds_ra_open(&api, argv[1]) != 0) {
        (void)fprintf(stderr, "HCCP/RA loader probe failed: %s\n", nds_ra_error(&api));
        if (runtime_service_open != 0) {
            (void)runtime_api.close_net_service();
        }
        nds_runtime_close(&runtime_api);
        return EXIT_FAILURE;
    }

    (void)printf("Calling RaInit for NPU physical ID %u in NETWORK_OFFLINE mode using HDC service type %d.\n",
                 config.phy_id, config.hdc_type);
    result = api.ra_init(&config);
    if (result != 0) {
        (void)fprintf(stderr, "RaInit failed: %d\n", result);
        nds_ra_close(&api);
        if (runtime_service_open != 0) {
            (void)runtime_api.close_net_service();
        }
        nds_runtime_close(&runtime_api);
        return EXIT_FAILURE;
    }
    (void)printf("RaInit succeeded; immediately calling RaDeinit without creating a device, QP, or MR.\n");
    result = api.ra_deinit(&config);
    if (result != 0) {
        (void)fprintf(stderr, "RaDeinit failed: %d\n", result);
        nds_ra_close(&api);
        if (runtime_service_open != 0) {
            (void)runtime_api.close_net_service();
        }
        nds_runtime_close(&runtime_api);
        return EXIT_FAILURE;
    }
    (void)printf("RaDeinit succeeded.\n");
    nds_ra_close(&api);

    if (runtime_service_open != 0) {
        result = runtime_api.close_net_service();
        if (result != 0) {
            (void)fprintf(stderr, "rtCloseNetService failed: %d\n", result);
            nds_runtime_close(&runtime_api);
            return EXIT_FAILURE;
        }
        (void)printf("rtCloseNetService succeeded.\n");
    }
    nds_runtime_close(&runtime_api);
    return EXIT_SUCCESS;
}

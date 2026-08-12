#include "nds/acl_loader.h"
#include "nds/ra_loader.h"
#include "nds/runtime_loader.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *program)
{
    (void)fprintf(stderr,
                  "usage: %s <path-to-libra.so> <npu-rnic-ipv4> <path-to-libruntime.so>\n"
                  "       <path-to-libascendcl.so> [--npu-physical-id N] [--logical-device-id N]\n",
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

static void print_gid(const uint8_t gid[16])
{
    size_t index;

    for (index = 0; index < 16; ++index) {
        (void)printf("%02x%s", gid[index], index == 15 ? "" : ":");
    }
}

int main(int argc, char **argv)
{
    nds_acl_api acl_api = {};
    nds_ra_api ra_api = {};
    nds_runtime_api runtime_api = {};
    nds_ra_init_config init_config = {
        .phy_id = NDS_RA_PHY_ID_NPU0,
        .nic_position = NDS_RA_NETWORK_OFFLINE,
        .hdc_type = NDS_RA_HDC_SERVICE_TYPE_RDMA_V2,
        .enable_hdc_async = false,
    };
    const char hdc_type_arg[] = "--hdcType=18";
    nds_rt_proc_ext_param parameter = {
        .param_info = hdc_type_arg,
        .param_len = sizeof(hdc_type_arg) - 1U,
    };
    nds_rt_net_service_open_args open_args = {
        .ext_param_list = &parameter,
        .ext_param_count = 1U,
    };
    nds_ra_rdev rdev = {};
    nds_ra_typical_qp qp_info = {};
    unsigned int logical_device_id = NDS_RA_PHY_ID_NPU0;
    void *rdev_handle = NULL;
    void *qp_handle = NULL;
    int acl_initialized = 0;
    int ra_initialized = 0;
    int service_open = 0;
    int result;
    int port_status = -1;
    int index;

    if (argc < 5) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }
    for (index = 5; index < argc; ++index) {
        unsigned int value;

        if (strcmp(argv[index], "--npu-physical-id") == 0) {
            if (++index >= argc || parse_uint(argv[index], &init_config.phy_id) != 0) {
                usage(argv[0]);
                return EXIT_FAILURE;
            }
            continue;
        }
        if (strcmp(argv[index], "--logical-device-id") == 0) {
            if (++index >= argc || parse_uint(argv[index], &value) != 0) {
                usage(argv[0]);
                return EXIT_FAILURE;
            }
            logical_device_id = value;
            continue;
        }
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    rdev.phy_id = init_config.phy_id;
    rdev.family = AF_INET;
    if (inet_pton(AF_INET, argv[2], &rdev.local_ip.ipv4) != 1) {
        (void)fprintf(stderr, "invalid NPU RNIC IPv4 address: %s\n", argv[2]);
        return EXIT_FAILURE;
    }

    if (nds_acl_open(&acl_api, argv[4]) != 0) {
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
    if (nds_runtime_open(&runtime_api, argv[3]) != 0) {
        (void)fprintf(stderr, "CANN runtime loader failed: %s\n", nds_runtime_error(&runtime_api));
        goto fail;
    }
    result = runtime_api.open_net_service(&open_args);
    if (result != 0) {
        (void)fprintf(stderr, "rtOpenNetService failed: %d\n", result);
        goto fail;
    }
    service_open = 1;

    if (nds_ra_open(&ra_api, argv[1]) != 0) {
        (void)fprintf(stderr, "HCCP/RA loader failed: %s\n", nds_ra_error(&ra_api));
        goto fail;
    }
    result = ra_api.ra_init(&init_config);
    if (result != 0) {
        (void)fprintf(stderr, "RaInit failed: %d\n", result);
        goto fail;
    }
    ra_initialized = 1;

    result = ra_api.ra_rdev_init(NDS_RA_NETWORK_OFFLINE, NDS_RA_NOTIFY, rdev, &rdev_handle);
    if (result != 0 || rdev_handle == NULL) {
        (void)fprintf(stderr, "RaRdevInit failed: %d\n", result);
        goto fail;
    }
    (void)printf("RaRdevInit succeeded for the selected NPU RNIC.\n");
    result = ra_api.ra_rdev_get_port_status(rdev_handle, &port_status);
    if (result != 0) {
        (void)fprintf(stderr, "RaRdevGetPortStatus failed: %d\n", result);
        goto fail;
    }
    if (port_status != NDS_RA_PORT_STATUS_ACTIVE) {
        (void)fprintf(stderr, "RaRdevGetPortStatus reported non-active status: %d\n", port_status);
        goto fail;
    }
    (void)printf("RaRdevGetPortStatus reported active (%d).\n", port_status);

    result = ra_api.ra_typical_qp_create(rdev_handle, NDS_RA_QP_FLAG_RC, NDS_RA_QP_MODE_OPBASE, &qp_info,
                                          &qp_handle);
    if (result != 0 || qp_handle == NULL) {
        (void)fprintf(stderr, "RaTypicalQpCreate failed: %d\n", result);
        goto fail;
    }
    (void)printf("RaTypicalQpCreate succeeded: qpn=%u psn=%u gid-index=%u gid=", qp_info.qpn, qp_info.psn,
                 qp_info.gid_index);
    print_gid(qp_info.gid);
    (void)printf("\n");

    result = ra_api.ra_qp_destroy(qp_handle);
    if (result != 0) {
        (void)fprintf(stderr, "RaQpDestroy failed: %d\n", result);
        qp_handle = NULL;
        goto fail;
    }
    qp_handle = NULL;
    (void)printf("RaQpDestroy succeeded.\n");

    result = ra_api.ra_rdev_deinit(rdev_handle, NDS_RA_NOTIFY);
    if (result != 0) {
        (void)fprintf(stderr, "RaRdevDeinit failed: %d\n", result);
        rdev_handle = NULL;
        goto fail;
    }
    rdev_handle = NULL;
    (void)printf("RaRdevDeinit succeeded.\n");

    result = ra_api.ra_deinit(&init_config);
    if (result != 0) {
        (void)fprintf(stderr, "RaDeinit failed: %d\n", result);
        ra_initialized = 0;
        goto fail;
    }
    ra_initialized = 0;
    nds_ra_close(&ra_api);

    result = runtime_api.close_net_service();
    if (result != 0) {
        (void)fprintf(stderr, "rtCloseNetService failed: %d\n", result);
        service_open = 0;
        goto fail;
    }
    service_open = 0;
    nds_runtime_close(&runtime_api);
    result = acl_api.finalize();
    if (result != 0) {
        (void)fprintf(stderr, "aclFinalize failed: %d\n", result);
        acl_initialized = 0;
        goto fail;
    }
    acl_initialized = 0;
    nds_acl_close(&acl_api);
    (void)printf("NPU rdev/QP lifecycle probe completed successfully.\n");
    return EXIT_SUCCESS;

fail:
    if (qp_handle != NULL) {
        (void)ra_api.ra_qp_destroy(qp_handle);
    }
    if (rdev_handle != NULL) {
        (void)ra_api.ra_rdev_deinit(rdev_handle, NDS_RA_NOTIFY);
    }
    if (ra_initialized != 0) {
        (void)ra_api.ra_deinit(&init_config);
    }
    nds_ra_close(&ra_api);
    if (service_open != 0) {
        (void)runtime_api.close_net_service();
    }
    nds_runtime_close(&runtime_api);
    if (acl_initialized != 0) {
        (void)acl_api.finalize();
    }
    nds_acl_close(&acl_api);
    return EXIT_FAILURE;
}

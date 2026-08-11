#ifndef NDS_RA_LOADER_H
#define NDS_RA_LOADER_H

#include <netinet/in.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ABI declarations independently transcribed from the HCCP v9.0.0 reference. */
enum {
    NDS_RA_NETWORK_PEER_ONLINE = 0,
    NDS_RA_NETWORK_OFFLINE = 1,
    NDS_RA_NOTIFY_NO_USE = 0,
    NDS_RA_NOTIFY = 1,
    NDS_RA_HDC_SERVICE_TYPE_RDMA = 6,
    NDS_RA_HDC_SERVICE_TYPE_RDMA_V2 = 18,
    NDS_RA_PHY_ID_NPU0 = 0,
    NDS_RA_QP_FLAG_RC = 0,
    NDS_RA_QP_MODE_OPBASE = 2,
    NDS_RA_ACCESS_LOCAL_WRITE = 1,
    NDS_RA_ACCESS_REMOTE_WRITE = 1 << 1,
    NDS_RA_ACCESS_REMOTE_READ = 1 << 2,
    /* HCOMM v9.0.0 TransportDirectNpu::RegUserMem access policy. */
    NDS_RA_ACCESS_DIRECT_NPU = NDS_RA_ACCESS_LOCAL_WRITE | NDS_RA_ACCESS_REMOTE_WRITE | NDS_RA_ACCESS_REMOTE_READ,
    NDS_RA_WR_RDMA_WRITE = 0,
    NDS_RA_SEND_SIGNALED = 1 << 1,
    /* rdma_lite_wc_status values returned through RaPollCq for an OPBASE Lite QP. */
    NDS_RA_WC_SUCCESS = 0,
    NDS_RA_WC_RETRY_EXCEEDED = 12,
    NDS_RA_QP_STATUS_NOT_CONNECTED = 0,
    NDS_RA_QP_STATUS_CONNECTED = 1,
    NDS_RA_QP_STATUS_TIMEOUT = 2,
    NDS_RA_QP_STATUS_CONNECTING = 3,
    NDS_RA_PORT_STATUS_DOWN = 0,
    NDS_RA_PORT_STATUS_ACTIVE = 1,
    NDS_RA_LITE_NOT_SUPPORTED = 0,
    NDS_RA_LITE_ALIGN_4K = 1,
    NDS_RA_LITE_ALIGN_2M = 2,
};

enum { NDS_RA_ERROR_CAPACITY = 512 };

typedef union nds_ra_ip_address {
    struct in_addr ipv4;
    struct in6_addr ipv6;
} nds_ra_ip_address;

typedef struct nds_ra_rdev {
    unsigned int phy_id;
    int family;
    nds_ra_ip_address local_ip;
} nds_ra_rdev;

typedef struct nds_ra_init_config {
    unsigned int phy_id;
    unsigned int nic_position;
    int hdc_type;
    bool enable_hdc_async;
} nds_ra_init_config;

typedef struct nds_ra_mr_info {
    void *address;
    unsigned long long size;
    int access;
    unsigned int local_key;
    unsigned int remote_key;
} nds_ra_mr_info;

typedef struct nds_ra_sge {
    uint64_t address;
    uint32_t length;
    uint32_t local_key;
} nds_ra_sge;

typedef struct nds_ra_send_wr {
    nds_ra_sge *buffers;
    uint16_t buffer_count;
    uint64_t remote_address;
    uint32_t remote_key;
    uint32_t opcode;
    int send_flags;
} nds_ra_send_wr;

typedef union nds_ra_send_response {
    struct {
        uint32_t sq_index;
        uint32_t wqe_index;
    } wqe;
    struct {
        uint32_t db_index;
        unsigned long db_info;
    } doorbell;
} nds_ra_send_response;

typedef struct nds_ra_cqe_error {
    uint32_t status;
    uint32_t qp_number;
    struct timeval time;
} nds_ra_cqe_error;

typedef struct nds_ra_completion {
    uint64_t wr_id;
    int status;
    int opcode;
    uint32_t vendor_error;
    uint32_t byte_length;
    uint32_t qp_number;
    uint32_t flags;
    uint32_t immediate_data_or_invalidated_rkey;
    uint16_t reserved[5];
    uint32_t version;
} nds_ra_completion;

/*
 * Locally queried QP attributes. This independently transcribed ABI object is
 * used only to obtain the NPU endpoint data needed by the NDS wire record; it
 * is never transmitted as-is.
 */
typedef struct nds_ra_qp_attr {
    uint32_t qpn;
    uint32_t udp_sport;
    uint32_t psn;
    uint32_t gid_index;
    uint8_t gid[16];
    int path_mtu;
    uint8_t feature[64];
} nds_ra_qp_attr;

/*
 * HCCP's local QP description.  This remains an ABI boundary type only:
 * NDS converts its project-owned wire record to and from this object rather
 * than sending this structure over the network.
 */
typedef struct nds_ra_typical_qp {
    uint32_t qpn;
    uint32_t psn;
    uint32_t gid_index;
    uint8_t compatibility_reserved_1[4];
    uint8_t gid[16];
    uint32_t traffic_class;
    uint32_t service_level;
    uint32_t retry_count;
    uint32_t retry_timeout;
    int version;
    uint32_t reserved[32];
    uint8_t compatibility_reserved_2[4];
} nds_ra_typical_qp;

typedef int (*nds_ra_init_fn)(nds_ra_init_config *config);
typedef int (*nds_ra_deinit_fn)(nds_ra_init_config *config);
typedef int (*nds_ra_rdev_init_fn)(int mode, unsigned int notify_type, nds_ra_rdev rdev, void **rdma_handle);
typedef int (*nds_ra_rdev_deinit_fn)(void *rdma_handle, unsigned int notify_type);
typedef int (*nds_ra_rdev_get_port_status_fn)(void *rdma_handle, int *status);
typedef int (*nds_ra_rdev_get_support_lite_fn)(void *rdma_handle, int *support_lite);
typedef int (*nds_ra_qp_create_fn)(void *rdma_handle, int flag, int qp_mode, void **qp_handle);
typedef int (*nds_ra_qp_connect_async_fn)(void *qp_handle, const void *fd_handle);
typedef int (*nds_ra_typical_qp_create_fn)(void *rdma_handle, int flag, int qp_mode,
                                            nds_ra_typical_qp *typical_qp_info, void **qp_handle);
typedef int (*nds_ra_typical_qp_modify_fn)(void *qp_handle, nds_ra_typical_qp *local_qp_info,
                                            nds_ra_typical_qp *remote_qp_info);
typedef int (*nds_ra_qp_destroy_fn)(void *qp_handle);
typedef int (*nds_ra_get_qp_attr_fn)(void *qp_handle, nds_ra_qp_attr *attributes);
typedef int (*nds_ra_get_qp_status_fn)(void *qp_handle, int *status);
typedef int (*nds_ra_rdev_get_cqe_error_list_fn)(void *rdma_handle, nds_ra_cqe_error *errors,
                                                  unsigned int *count);
typedef int (*nds_ra_register_mr_fn)(const void *rdma_handle, nds_ra_mr_info *info, void **mr_handle);
typedef int (*nds_ra_deregister_mr_fn)(const void *rdma_handle, void *mr_handle);
typedef int (*nds_ra_typical_send_wr_fn)(void *qp_handle, nds_ra_send_wr *wr, nds_ra_send_response *response);
typedef int (*nds_ra_poll_cq_fn)(void *qp_handle, bool is_send_cq, unsigned int max_entries, void *completions);

/*
 * Runtime-only loader for the HCCP/RA shared-library ABI.
 *
 * Typed signatures are introduced incrementally, after their matching source
 * declarations and structure layout have been reviewed. Opaque HCCP handles
 * remain `void *` across this ABI boundary.
 */
typedef struct nds_ra_api {
    void *library;
    nds_ra_init_fn ra_init;
    nds_ra_deinit_fn ra_deinit;
    nds_ra_rdev_init_fn ra_rdev_init;
    nds_ra_rdev_deinit_fn ra_rdev_deinit;
    nds_ra_rdev_get_port_status_fn ra_rdev_get_port_status;
    nds_ra_rdev_get_support_lite_fn ra_rdev_get_support_lite;
    nds_ra_qp_create_fn ra_qp_create;
    nds_ra_qp_connect_async_fn ra_qp_connect_async;
    nds_ra_typical_qp_create_fn ra_typical_qp_create;
    nds_ra_typical_qp_modify_fn ra_typical_qp_modify;
    nds_ra_qp_destroy_fn ra_qp_destroy;
    nds_ra_get_qp_attr_fn ra_get_qp_attr;
    nds_ra_get_qp_status_fn ra_get_qp_status;
    nds_ra_rdev_get_cqe_error_list_fn ra_rdev_get_cqe_error_list;
    nds_ra_register_mr_fn ra_register_mr;
    nds_ra_deregister_mr_fn ra_deregister_mr;
    nds_ra_typical_send_wr_fn ra_typical_send_wr;
    nds_ra_poll_cq_fn ra_poll_cq;
    char error[NDS_RA_ERROR_CAPACITY];
} nds_ra_api;

int nds_ra_open(nds_ra_api *api, const char *library_path);
void nds_ra_close(nds_ra_api *api);
const char *nds_ra_error(const nds_ra_api *api);

#ifdef __cplusplus
}
#endif

#endif

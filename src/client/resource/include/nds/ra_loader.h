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
    NDS_RA_QP_MODE_NORMAL = 0,
    NDS_RA_QP_MODE_OPBASE = 2,
    NDS_RA_QP_MODE_OPBASE_EXT = 4,
    NDS_RA_AI_CALLER_POLLS_CQ = 1U << 0,
    NDS_RA_QP_CREATE_WITH_ATTR_VERSION = 1,
    NDS_RA_QP_TYPE_RC = 2,
    NDS_RA_ACCESS_LOCAL_WRITE = 1,
    NDS_RA_ACCESS_REMOTE_WRITE = 1 << 1,
    NDS_RA_ACCESS_REMOTE_READ = 1 << 2,
    /* HCOMM v9.0.0 TransportDirectNpu::RegUserMem access policy. */
    NDS_RA_ACCESS_DIRECT_NPU = NDS_RA_ACCESS_LOCAL_WRITE | NDS_RA_ACCESS_REMOTE_WRITE | NDS_RA_ACCESS_REMOTE_READ,
    NDS_RA_WR_RDMA_WRITE = 0,
    NDS_RA_WR_SEND = 2,
    NDS_RA_WR_RDMA_READ = 4,
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
    NDS_RA_MAX_INTERFACE_COUNT = 64,
};

enum { NDS_RA_ERROR_CAPACITY = 512 };

typedef union NdsRaIpAddress {
    struct in_addr ipv4;
    struct in6_addr ipv6;
} NdsRaIpAddress;

typedef struct NdsRaRdev {
    unsigned int phy_id;
    int family;
    NdsRaIpAddress local_ip;
} NdsRaRdev;

typedef struct NdsRaGetInterfaceConfig {
    unsigned int physical_device_id;
    unsigned int network_mode;
    bool all_interfaces;
} NdsRaGetInterfaceConfig;

typedef struct NdsRaInterfaceAddress {
    NdsRaIpAddress ip;
    struct in_addr netmask;
} NdsRaInterfaceAddress;

typedef struct NdsRaInterfaceInfo {
    int family;
    int scope_id;
    NdsRaInterfaceAddress address;
    char name[256];
} NdsRaInterfaceInfo;

#if defined(__cplusplus)
static_assert(sizeof(NdsRaGetInterfaceConfig) == 12, "HCCP RaGetIfattr ABI must remain 12 bytes");
static_assert(sizeof(NdsRaInterfaceInfo) == 284, "HCCP InterfaceInfo ABI must remain 284 bytes");
#else
_Static_assert(sizeof(NdsRaGetInterfaceConfig) == 12, "HCCP RaGetIfattr ABI must remain 12 bytes");
_Static_assert(sizeof(NdsRaInterfaceInfo) == 284, "HCCP InterfaceInfo ABI must remain 284 bytes");
#endif

/*
 * HCCP v9.0.0 RdevInitInfo.  The legacy RaRdevInit() hard-codes
 * disabled_lite_thread=false, which starts HCCP's agent-side Lite-QP poller.
 * That poller is separate from the service-side verbs completion-channel path
 * used by AI QPs.
 */
typedef struct NdsRaRdevInitInfo {
    int mode;
    unsigned int notify_type;
    bool enabled_910a_lite;
    bool disabled_lite_thread;
    bool enabled_2mb_lite;
} NdsRaRdevInitInfo;

typedef struct NdsRaInitConfig {
    unsigned int phy_id;
    unsigned int nic_position;
    int hdc_type;
    bool enable_hdc_async;
} NdsRaInitConfig;

typedef struct NdsRaMrInfo {
    void *address;
    unsigned long long size;
    int access;
    unsigned int local_key;
    unsigned int remote_key;
} NdsRaMrInfo;

typedef struct NdsRaSge {
    uint64_t address;
    uint32_t length;
    uint32_t local_key;
} NdsRaSge;

typedef struct NdsRaSendWr {
    NdsRaSge *buffers;
    uint16_t buffer_count;
    uint64_t remote_address;
    uint32_t remote_key;
    uint32_t opcode;
    int send_flags;
} NdsRaSendWr;

typedef struct NdsRaRecvWr {
    uint64_t wr_id;
    NdsRaSge memory;
} NdsRaRecvWr;

typedef union NdsRaSendResponse {
    struct {
        uint32_t sq_index;
        uint32_t wqe_index;
    } wqe;
    struct {
        uint32_t db_index;
        unsigned long db_info;
    } doorbell;
} NdsRaSendResponse;

typedef struct NdsRaCqeError {
    uint32_t status;
    uint32_t qp_number;
    struct timeval time;
} NdsRaCqeError;

typedef struct NdsRaCompletion {
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
} NdsRaCompletion;

/*
 * Locally queried QP attributes. This independently transcribed ABI object is
 * used only to obtain the NPU endpoint data needed by the NDS wire record; it
 * is never transmitted as-is.
 */
typedef struct NdsRaQpAttr {
    uint32_t qpn;
    uint32_t udp_sport;
    uint32_t psn;
    uint32_t gid_index;
    uint8_t gid[16];
    int path_mtu;
    uint8_t feature[64];
} NdsRaQpAttr;

/*
 * HCCP's local QP description.  This remains an ABI boundary type only:
 * NDS converts its project-owned wire record to and from this object rather
 * than sending this structure over the network.
 */
/* CANN 9.0.0 HCCP QpExtAttrs: project-owned transcription with the
 * embedded ibv_qp_init_attr represented as its verified 64-byte ABI image.
 * The AICPU path needs only the initialized fields below. */
typedef struct NdsRaQpCap {
    uint32_t max_send_wr;
    uint32_t max_recv_wr;
    uint32_t max_send_sge;
    uint32_t max_recv_sge;
    uint32_t max_inline_data;
} NdsRaQpCap;

typedef struct NdsRaQpInitAttr {
    void *qp_context;
    void *send_cq;
    void *recv_cq;
    void *srq;
    NdsRaQpCap cap;
    int qp_type;
    int sq_sig_all;
    uint8_t reserved[4];
} NdsRaQpInitAttr;

typedef struct NdsRaCqExtAttr {
    int send_cq_depth;
    int recv_cq_depth;
    int send_cq_comp_vector;
    int recv_cq_comp_vector;
} NdsRaCqExtAttr;

typedef struct NdsRaQosAttr {
    uint8_t traffic_class;
    uint8_t service_level;
    uint8_t reserved[6];
} NdsRaQosAttr;

typedef struct NdsRaQpExtAttrs {
    int qp_mode;
    NdsRaCqExtAttr cq_attr;
    NdsRaQpInitAttr qp_attr;
    int version;
    int mem_align;
    uint32_t udp_sport;
    uint32_t data_plane_flag;
    uint32_t reserved[29];
} NdsRaQpExtAttrs;

typedef struct NdsRaAiQpInfo {
    uint64_t ai_qp_address;
    uint32_t sq_index;
    uint32_t db_index;
    uint64_t ai_scq_address;
    uint64_t ai_rcq_address;
    uint8_t data_plane_info[336];
} NdsRaAiQpInfo;

/* HCCP v9.0.0 AI-QP dataplane records. They are copied into NDS-owned
 * device records and are never sent to the CPU peer. */
typedef struct NdsRaAiDataPlaneWq {
    uint32_t wqn;
    uint32_t compatibility_padding;
    uint64_t buffer_address;
    uint32_t wqebb_size;
    uint32_t depth;
    uint64_t head_address;
    uint64_t tail_address;
    uint64_t software_doorbell_address;
    uint64_t doorbell_register_address;
    uint32_t reserved[8];
} NdsRaAiDataPlaneWq;

typedef struct NdsRaAiDataPlaneCq {
    uint32_t cqn;
    uint32_t compatibility_padding;
    uint64_t buffer_address;
    uint32_t cqe_size;
    uint32_t depth;
    uint64_t head_address;
    uint64_t tail_address;
    uint64_t software_doorbell_address;
    uint64_t doorbell_register_address;
    uint32_t reserved[2];
} NdsRaAiDataPlaneCq;

typedef struct NdsRaAiDataPlaneInfo {
    NdsRaAiDataPlaneWq send_wq;
    NdsRaAiDataPlaneWq receive_wq;
    NdsRaAiDataPlaneCq send_cq;
    NdsRaAiDataPlaneCq receive_cq;
    uint32_t reserved[8];
} NdsRaAiDataPlaneInfo;

#if defined(__cplusplus)
static_assert(sizeof(NdsRaAiDataPlaneWq) == 88, "HCCP AiDataPlaneWq ABI must remain 88 bytes");
static_assert(sizeof(NdsRaAiDataPlaneCq) == 64, "HCCP AiDataPlaneCq ABI must remain 64 bytes");
static_assert(sizeof(NdsRaAiDataPlaneInfo) == 336, "HCCP AiDataPlaneInfo ABI must remain 336 bytes");
#else
_Static_assert(sizeof(NdsRaAiDataPlaneWq) == 88, "HCCP AiDataPlaneWq ABI must remain 88 bytes");
_Static_assert(sizeof(NdsRaAiDataPlaneCq) == 64, "HCCP AiDataPlaneCq ABI must remain 64 bytes");
_Static_assert(sizeof(NdsRaAiDataPlaneInfo) == 336, "HCCP AiDataPlaneInfo ABI must remain 336 bytes");
#endif

typedef struct NdsRaTypicalQp {
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
} NdsRaTypicalQp;

typedef int (*NdsRaInitFn)(NdsRaInitConfig *config);
typedef int (*NdsRaDeinitFn)(NdsRaInitConfig *config);
typedef int (*NdsRaRdevInitFn)(int mode, unsigned int notify_type, NdsRaRdev rdev, void **rdma_handle);
typedef int (*NdsRaRdevInitV2Fn)(NdsRaRdevInitInfo init_info, NdsRaRdev rdev, void **rdma_handle);
typedef int (*NdsRaRdevDeinitFn)(void *rdma_handle, unsigned int notify_type);
typedef int (*NdsRaRdevGetPortStatusFn)(void *rdma_handle, int *status);
typedef int (*NdsRaRdevGetSupportLiteFn)(void *rdma_handle, int *support_lite);
typedef int (*NdsRaQpCreateFn)(void *rdma_handle, int flag, int qp_mode, void **qp_handle);
typedef int (*NdsRaQpConnectAsyncFn)(void *qp_handle, const void *fd_handle);
typedef int (*NdsRaTypicalQpCreateFn)(void *rdma_handle, int flag, int qp_mode, NdsRaTypicalQp *typical_qp_info,
                                           void **qp_handle);
typedef int (*NdsRaAiQpCreateFn)(void *rdma_handle, NdsRaQpExtAttrs *attrs, NdsRaAiQpInfo *info,
                                      void **qp_handle);
typedef int (*NdsRaSetQpAttrQosFn)(void *qp_handle, NdsRaQosAttr *attr);
typedef int (*NdsRaSetQpAttrTimeoutFn)(void *qp_handle, uint32_t *timeout);
typedef int (*NdsRaSetQpAttrRetryCountFn)(void *qp_handle, uint32_t *retry_count);
typedef int (*NdsRaTypicalQpModifyFn)(void *qp_handle, NdsRaTypicalQp *local_qp_info,
                                           NdsRaTypicalQp *remote_qp_info);
typedef int (*NdsRaQpDestroyFn)(void *qp_handle);
typedef int (*NdsRaGetQpAttrFn)(void *qp_handle, NdsRaQpAttr *attributes);
typedef int (*NdsRaGetQpStatusFn)(void *qp_handle, int *status);
typedef int (*NdsRaRdevGetCqeErrorListFn)(void *rdma_handle, NdsRaCqeError *errors, unsigned int *count);
typedef int (*NdsRaRegisterMrFn)(const void *rdma_handle, NdsRaMrInfo *info, void **mr_handle);
typedef int (*NdsRaDeregisterMrFn)(const void *rdma_handle, void *mr_handle);
typedef int (*NdsRaTypicalSendWrFn)(void *qp_handle, NdsRaSendWr *wr, NdsRaSendResponse *response);
typedef int (*NdsRaRecvWrlistFn)(void *qp_handle, NdsRaRecvWr *wr, unsigned int recv_num,
                                     unsigned int *complete_num);
typedef int (*NdsRaPollCqFn)(void *qp_handle, bool is_send_cq, unsigned int max_entries, void *completions);
typedef int (*NdsRaGetInterfaceCountFn)(NdsRaGetInterfaceConfig *config, unsigned int *count);
typedef int (*NdsRaGetInterfacesFn)(NdsRaGetInterfaceConfig *config, NdsRaInterfaceInfo *interfaces,
                                        unsigned int *count);

/*
 * Runtime-only loader for the HCCP/RA shared-library ABI.
 *
 * Typed signatures are introduced incrementally, after their matching source
 * declarations and structure layout have been reviewed. Opaque HCCP handles
 * remain `void *` across this ABI boundary.
 */
typedef struct NdsRaApi {
    void *library;
    NdsRaInitFn ra_init;
    NdsRaDeinitFn ra_deinit;
    NdsRaRdevInitFn ra_rdev_init;
    NdsRaRdevInitV2Fn ra_rdev_init_v2;
    NdsRaRdevDeinitFn ra_rdev_deinit;
    NdsRaRdevGetPortStatusFn ra_rdev_get_port_status;
    NdsRaRdevGetSupportLiteFn ra_rdev_get_support_lite;
    NdsRaQpCreateFn ra_qp_create;
    NdsRaQpConnectAsyncFn ra_qp_connect_async;
    NdsRaTypicalQpCreateFn ra_typical_qp_create;
    NdsRaAiQpCreateFn ra_ai_qp_create;
    NdsRaSetQpAttrQosFn ra_set_qp_attr_qos;
    NdsRaSetQpAttrTimeoutFn ra_set_qp_attr_timeout;
    NdsRaSetQpAttrRetryCountFn ra_set_qp_attr_retry_count;
    NdsRaTypicalQpModifyFn ra_typical_qp_modify;
    NdsRaQpDestroyFn ra_qp_destroy;
    NdsRaGetQpAttrFn ra_get_qp_attr;
    NdsRaGetQpStatusFn ra_get_qp_status;
    NdsRaRdevGetCqeErrorListFn ra_rdev_get_cqe_error_list;
    NdsRaRegisterMrFn ra_register_mr;
    NdsRaDeregisterMrFn ra_deregister_mr;
    NdsRaTypicalSendWrFn ra_typical_send_wr;
    NdsRaRecvWrlistFn ra_recv_wrlist;
    NdsRaPollCqFn ra_poll_cq;
    NdsRaGetInterfaceCountFn ra_get_interface_count;
    NdsRaGetInterfacesFn ra_get_interfaces;
    char error[NDS_RA_ERROR_CAPACITY];
} NdsRaApi;

int nds_ra_open(NdsRaApi *api, const char *library_path);
void nds_ra_close(NdsRaApi *api);
const char *nds_ra_error(const NdsRaApi *api);

#ifdef __cplusplus
}
#endif

#endif

#ifndef NDS_CLIENT_LOADERS_LIBRA_HH
#define NDS_CLIENT_LOADERS_LIBRA_HH

#include "result.hh"
#include "shared_library.hh"

#include <netinet/in.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/time.h>

#include <string_view>

class Libra {
public:
    /* ABI constants independently transcribed from the HCCP v9.0.0 reference. */
    enum {
        NETWORK_PEER_ONLINE = 0,
        NETWORK_OFFLINE = 1,
        NOTIFY_NO_USE = 0,
        NOTIFY = 1,
        HDC_SERVICE_TYPE_RDMA = 6,
        HDC_SERVICE_TYPE_RDMA_V2 = 18,
        PHYSICAL_DEVICE_NPU0 = 0,
        QP_FLAG_RC = 0,
        QP_MODE_NORMAL = 0,
        QP_MODE_OPBASE = 2,
        QP_MODE_OPBASE_EXT = 4,
        AI_CALLER_POLLS_CQ = 1U << 0,
        QP_CREATE_WITH_ATTR_VERSION = 1,
        QP_TYPE_RC = 2,
        ACCESS_LOCAL_WRITE = 1,
        ACCESS_REMOTE_WRITE = 1 << 1,
        ACCESS_REMOTE_READ = 1 << 2,
        WR_RDMA_WRITE = 0,
        WR_SEND = 2,
        WR_RDMA_READ = 4,
        SEND_SIGNALED = 1 << 1,
        /* Provider-specific rdma_lite_wc_status value returned for retry exhaustion. */
        WC_RETRY_EXCEEDED = 12,
        QP_STATUS_NOT_CONNECTED = 0,
        QP_STATUS_CONNECTED = 1,
        QP_STATUS_TIMEOUT = 2,
        QP_STATUS_CONNECTING = 3,
        PORT_STATUS_DOWN = 0,
        PORT_STATUS_ACTIVE = 1,
        LITE_NOT_SUPPORTED = 0,
        LITE_ALIGN_4K = 1,
        LITE_ALIGN_2M = 2,
        MAX_INTERFACE_COUNT = 64,
    };

    union IpAddress {
        struct in_addr ipv4;
        struct in6_addr ipv6;
    };

    struct Rdev {
        unsigned int phy_id;
        int family;
        IpAddress local_ip;
    };

    struct GetInterfaceConfig {
        unsigned int physical_device_id;
        unsigned int network_mode;
        bool all_interfaces;
    };

    struct InterfaceAddress {
        IpAddress ip;
        struct in_addr netmask;
    };

    struct InterfaceInfo {
        int family;
        int scope_id;
        InterfaceAddress address;
        char name[256];
    };

    static_assert(sizeof(GetInterfaceConfig) == 12, "HCCP RaGetIfattr ABI must remain 12 bytes");
    static_assert(sizeof(InterfaceInfo) == 284, "HCCP InterfaceInfo ABI must remain 284 bytes");

    /*
     * HCCP v9.0.0 RdevInitInfo.  The legacy RaRdevInit() hard-codes
     * disabled_lite_thread=false, which starts HCCP's agent-side Lite-QP poller.
     * That poller is separate from the service-side verbs completion-channel path
     * used by AI QPs.
     */
    struct RdevInitInfo {
        int mode;
        unsigned int notify_type;
        bool enabled_910a_lite;
        bool disabled_lite_thread;
        bool enabled_2mb_lite;
    };

    struct InitConfig {
        unsigned int phy_id;
        unsigned int nic_position;
        int hdc_type;
        bool enable_hdc_async;
    };

    struct MrInfo {
        void *address;
        unsigned long long size;
        int access;
        unsigned int local_key;
        unsigned int remote_key;
    };

    struct Sge {
        uint64_t address;
        uint32_t length;
        uint32_t local_key;
    };

    struct SendWr {
        Sge *buffers;
        uint16_t buffer_count;
        uint64_t remote_address;
        uint32_t remote_key;
        uint32_t opcode;
        int send_flags;
    };

    struct RecvWr {
        uint64_t wr_id;
        Sge memory;
    };

    union SendResponse {
        struct {
            uint32_t sq_index;
            uint32_t wqe_index;
        } wqe;
        struct {
            uint32_t db_index;
            unsigned long db_info;
        } doorbell;
    };

    struct CqeError {
        uint32_t status;
        uint32_t qp_number;
        struct timeval time;
    };

    struct Completion {
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
    };

    /*
     * Locally queried QP attributes. This independently transcribed ABI object is
     * used only to obtain the NPU endpoint data needed by the NDS wire record; it
     * is never transmitted as-is.
     */
    struct QpAttr {
        uint32_t qpn;
        uint32_t udp_sport;
        uint32_t psn;
        uint32_t gid_index;
        uint8_t gid[16];
        int path_mtu;
        uint8_t feature[64];
    };

    /*
     * HCCP's local QP description.  This remains an ABI boundary type only:
     * NDS converts its project-owned wire record to and from this object rather
     * than sending this structure over the network.
     */
    /* CANN 9.0.0 HCCP QpExtAttrs: project-owned transcription with the
     * embedded ibv_qp_init_attr represented as its verified 64-byte ABI image.
     * The AICPU path needs only the initialized fields below. */
    struct QpCap {
        uint32_t max_send_wr;
        uint32_t max_recv_wr;
        uint32_t max_send_sge;
        uint32_t max_recv_sge;
        uint32_t max_inline_data;
    };

    struct QpInitAttr {
        void *qp_context;
        void *send_cq;
        void *recv_cq;
        void *srq;
        QpCap cap;
        int qp_type;
        int sq_sig_all;
        uint8_t reserved[4];
    };

    struct CqExtAttr {
        int send_cq_depth;
        int recv_cq_depth;
        int send_cq_comp_vector;
        int recv_cq_comp_vector;
    };

    struct QosAttr {
        uint8_t traffic_class;
        uint8_t service_level;
        uint8_t reserved[6];
    };

    struct QpExtAttrs {
        int qp_mode;
        CqExtAttr cq_attr;
        QpInitAttr qp_attr;
        int version;
        int mem_align;
        uint32_t udp_sport;
        uint32_t data_plane_flag;
        uint32_t reserved[29];
    };

    struct AiQpInfo {
        uint64_t ai_qp_address;
        uint32_t sq_index;
        uint32_t db_index;
        uint64_t ai_scq_address;
        uint64_t ai_rcq_address;
        uint8_t data_plane_info[336];
    };

    /* HCCP v9.0.0 AI-QP dataplane records. They are copied into NDS-owned
     * device records and are never sent to the CPU peer. */
    struct AiDataPlaneWq {
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
    };

    struct AiDataPlaneCq {
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
    };

    struct AiDataPlaneInfo {
        AiDataPlaneWq send_wq;
        AiDataPlaneWq receive_wq;
        AiDataPlaneCq send_cq;
        AiDataPlaneCq receive_cq;
        uint32_t reserved[8];
    };

    static_assert(sizeof(AiDataPlaneWq) == 88, "HCCP AiDataPlaneWq ABI must remain 88 bytes");
    static_assert(sizeof(AiDataPlaneCq) == 64, "HCCP AiDataPlaneCq ABI must remain 64 bytes");
    static_assert(sizeof(AiDataPlaneInfo) == 336, "HCCP AiDataPlaneInfo ABI must remain 336 bytes");

    struct TypicalQp {
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
    };

    /*
     * Runtime-only loader for the HCCP/RA shared-library ABI.
     *
     * Typed signatures are introduced incrementally, after their matching source
     * declarations and structure layout have been reviewed. Opaque HCCP handles
     * remain `void *` across this ABI boundary.
     */
    /* Move-only owner of libra.so and its resolved ABI symbols. */
    using InitFn = int (*)(InitConfig *config);
    using DeinitFn = int (*)(InitConfig *config);
    using RdevInitFn = int (*)(int mode, unsigned int notify_type, Rdev rdev, void **rdma_handle);
    using RdevInitV2Fn = int (*)(RdevInitInfo init_info, Rdev rdev, void **rdma_handle);
    using RdevDeinitFn = int (*)(void *rdma_handle, unsigned int notify_type);
    using RdevGetPortStatusFn = int (*)(void *rdma_handle, int *status);
    using RdevGetSupportLiteFn = int (*)(void *rdma_handle, int *support_lite);
    using QpCreateFn = int (*)(void *rdma_handle, int flag, int qp_mode, void **qp_handle);
    using QpConnectAsyncFn = int (*)(void *qp_handle, const void *fd_handle);
    using TypicalQpCreateFn = int (*)(void *rdma_handle, int flag, int qp_mode, TypicalQp *typical_qp_info,
                                      void **qp_handle);
    using AiQpCreateFn = int (*)(void *rdma_handle, QpExtAttrs *attrs, AiQpInfo *info, void **qp_handle);
    using SetQpAttrQosFn = int (*)(void *qp_handle, QosAttr *attr);
    using SetQpAttrTimeoutFn = int (*)(void *qp_handle, uint32_t *timeout);
    using SetQpAttrRetryCountFn = int (*)(void *qp_handle, uint32_t *retry_count);
    using TypicalQpModifyFn = int (*)(void *qp_handle, TypicalQp *local_qp_info, TypicalQp *remote_qp_info);
    using QpDestroyFn = int (*)(void *qp_handle);
    using GetQpAttrFn = int (*)(void *qp_handle, QpAttr *attributes);
    using GetQpStatusFn = int (*)(void *qp_handle, int *status);
    using RdevGetCqeErrorListFn = int (*)(void *rdma_handle, CqeError *errors, unsigned int *count);
    using RegisterMrFn = int (*)(const void *rdma_handle, MrInfo *info, void **mr_handle);
    using DeregisterMrFn = int (*)(const void *rdma_handle, void *mr_handle);
    using TypicalSendWrFn = int (*)(void *qp_handle, SendWr *wr, SendResponse *response);
    using RecvWrlistFn = int (*)(void *qp_handle, RecvWr *wr, unsigned int recv_num, unsigned int *complete_num);
    using PollCqFn = int (*)(void *qp_handle, bool is_send_cq, unsigned int max_entries, void *completions);
    using GetInterfaceCountFn = int (*)(GetInterfaceConfig *config, unsigned int *count);
    using GetInterfacesFn = int (*)(GetInterfaceConfig *config, InterfaceInfo *interfaces, unsigned int *count);

    Libra() = default;
    ~Libra() = default;
    Libra(const Libra &) = delete;
    Libra &operator=(const Libra &) = delete;
    Libra(Libra &&) noexcept = default;
    Libra &operator=(Libra &&) noexcept = default;

    /* Production loads the CANN-exported SONAME; tests may supply a fake path. */
    static nds::Result<Libra> open(std::string_view library_path = "libra.so");

    InitFn init{};
    DeinitFn deinit{};
    RdevInitFn rdev_init{};
    RdevInitV2Fn rdev_init_v2{};
    RdevDeinitFn rdev_deinit{};
    RdevGetPortStatusFn rdev_get_port_status{};
    RdevGetSupportLiteFn rdev_get_support_lite{};
    QpCreateFn qp_create{};
    QpConnectAsyncFn qp_connect_async{};
    TypicalQpCreateFn typical_qp_create{};
    AiQpCreateFn ai_qp_create{};
    SetQpAttrQosFn set_qp_attr_qos{};
    SetQpAttrTimeoutFn set_qp_attr_timeout{};
    SetQpAttrRetryCountFn set_qp_attr_retry_count{};
    TypicalQpModifyFn typical_qp_modify{};
    QpDestroyFn qp_destroy{};
    GetQpAttrFn get_qp_attr{};
    GetQpStatusFn get_qp_status{};
    RdevGetCqeErrorListFn rdev_get_cqe_error_list{};
    RegisterMrFn register_mr{};
    DeregisterMrFn deregister_mr{};
    TypicalSendWrFn typical_send_wr{};
    RecvWrlistFn recv_wrlist{};
    PollCqFn poll_cq{};
    GetInterfaceCountFn get_interface_count{};
    GetInterfacesFn get_interfaces{};

private:
    nds::client::SharedLibrary library_;
};

#endif

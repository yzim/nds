#define _POSIX_C_SOURCE 200809L

#include "nds/rdma_path_mtu.h"
#include "nds/rdma_wire_codec.h"
#include "nds/logging.hh"

#include <CLI/CLI.hpp>

#include <arpa/inet.h>
#include <errno.h>
#include <infiniband/verbs.h>
#include <limits.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <string>
#include <vector>

#define NDS_DEFAULT_LISTEN "0.0.0.0"
#define NDS_DEFAULT_TCP_PORT 18515U
#define NDS_DEFAULT_IB_PORT 1U
#define NDS_UNSET_GID_INDEX UINT_MAX
#define NDS_QP_DEPTH 16U
#define NDS_DEFAULT_BYTES 4096U
#define NDS_MAX_BYTES (64U * 1024U)
#define NDS_GUARD_BYTES 64U
#define NDS_GUARD_VALUE 0xa5U
#define NDS_PAYLOAD_INITIAL_VALUE 0xccU
#define NDS_TRANSFER_WAIT_MS 5000U

struct nds_server_config {
    std::string device_name;
    std::string listen_address{NDS_DEFAULT_LISTEN};
    unsigned int tcp_port;
    unsigned int ib_port;
    unsigned int gid_index;
    unsigned int bytes;
    unsigned int post_close_hold_ms;
    bool qp_only;
    std::string log_sink{"stderr"};
    std::string log_level{"info"};
};

struct nds_verbs_resources {
    ibv_context *context = nullptr;
    ibv_pd *pd = nullptr;
    ibv_cq *cq = nullptr;
    ibv_qp *qp = nullptr;
    uint32_t psn = 0U;
    uint32_t path_mtu = 0U;
    ibv_gid gid = {};
    std::vector<unsigned char> buffer;
    size_t buffer_length = 0U;
    ibv_mr *mr = nullptr;

    nds_verbs_resources() = default;
    nds_verbs_resources(const nds_verbs_resources &) = delete;
    nds_verbs_resources &operator=(const nds_verbs_resources &) = delete;

    ~nds_verbs_resources()
    {
        if (mr != nullptr) (void)ibv_dereg_mr(mr);
        if (qp != nullptr) (void)ibv_destroy_qp(qp);
        if (cq != nullptr) (void)ibv_destroy_cq(cq);
        if (pd != nullptr) (void)ibv_dealloc_pd(pd);
        if (context != nullptr) (void)ibv_close_device(context);
    }
};

class nds_file_descriptor {
public:
    explicit nds_file_descriptor(int descriptor = -1) : descriptor_(descriptor) {}
    nds_file_descriptor(const nds_file_descriptor &) = delete;
    nds_file_descriptor &operator=(const nds_file_descriptor &) = delete;
    ~nds_file_descriptor()
    {
        if (descriptor_ >= 0) (void)close(descriptor_);
    }

    int get() const { return descriptor_; }
    void reset(int descriptor = -1)
    {
        if (descriptor_ >= 0) (void)close(descriptor_);
        descriptor_ = descriptor;
    }

private:
    int descriptor_;
};

class nds_device_list {
public:
    ibv_device **devices = nullptr;
    nds_device_list(const nds_device_list &) = delete;
    nds_device_list &operator=(const nds_device_list &) = delete;
    nds_device_list() = default;
    ~nds_device_list()
    {
        if (devices != nullptr) ibv_free_device_list(devices);
    }
};

static int parse_arguments(int argc, char **argv, struct nds_server_config *config, bool &exit_requested)
{
    *config = {};
    config->tcp_port = NDS_DEFAULT_TCP_PORT;
    config->ib_port = NDS_DEFAULT_IB_PORT;
    config->gid_index = NDS_UNSET_GID_INDEX;
    config->bytes = NDS_DEFAULT_BYTES;
    config->post_close_hold_ms = 0U;
    CLI::App app{"Create one CPU verbs RC QP and receive an NPU RDMA Write."};
    app.add_option("--device", config->device_name, "RDMA device name")->required();
    app.add_option("--gid-index", config->gid_index, "RoCE GID index")->required()->check(CLI::Range(0U, static_cast<unsigned int>(INT32_MAX)));
    app.add_option("--listen", config->listen_address, "TCP listen IPv4 address");
    app.add_option("--tcp-port", config->tcp_port, "TCP peer-exchange port")->check(CLI::Range(1U, static_cast<unsigned int>(UINT16_MAX)));
    app.add_option("--ib-port", config->ib_port, "RDMA port")->check(CLI::Range(1U, static_cast<unsigned int>(UINT8_MAX)));
    app.add_option("--bytes", config->bytes, "Destination buffer size")->check(CLI::Range(1U, NDS_MAX_BYTES));
    app.add_option("--post-close-hold-ms", config->post_close_hold_ms, "Passive diagnostic hold time")->check(CLI::Range(0U, 60000U));
    app.add_flag("--qp-only", config->qp_only, "Validate QP establishment only");
    app.add_option("--log-sink", config->log_sink, "Log sink")->check(CLI::IsMember({"stderr", "stdout", "syslog", "none"}));
    app.add_option("--log-level", config->log_level, "Log level")
        ->check(CLI::IsMember({"trace", "debug", "info", "warn", "error", "critical", "off"}));
    try {
        app.parse(argc, argv);
    } catch (const CLI::CallForHelp &help) {
        exit_requested = true;
        return app.exit(help);
    } catch (const CLI::ParseError &parse_error) {
        return app.exit(parse_error);
    }
    return 0;
}

static int read_full(int descriptor, void *buffer, size_t length)
{
    unsigned char *cursor = static_cast<unsigned char *>(buffer);

    while (length != 0) {
        const ssize_t result = read(descriptor, cursor, length);
        if (result == 0) {
            return -1;
        }
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        cursor += (size_t)result;
        length -= (size_t)result;
    }
    return 0;
}

static int write_full(int descriptor, const void *buffer, size_t length)
{
    const unsigned char *cursor = static_cast<const unsigned char *>(buffer);

    while (length != 0) {
        const ssize_t result = write(descriptor, cursor, length);
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        cursor += (size_t)result;
        length -= (size_t)result;
    }
    return 0;
}

static uint32_t make_psn(void)
{
    struct timespec now;

    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return ((uint32_t)now.tv_nsec ^ (uint32_t)getpid()) & UINT32_C(0x00ffffff);
}

static uint32_t mtu_to_bytes(enum ibv_mtu mtu)
{
    switch (mtu) {
        case IBV_MTU_256:
            return 256U;
        case IBV_MTU_512:
            return 512U;
        case IBV_MTU_1024:
            return 1024U;
        case IBV_MTU_2048:
            return 2048U;
        case IBV_MTU_4096:
            return 4096U;
        default:
            return 0U;
    }
}

static int mtu_from_bytes(uint32_t bytes, enum ibv_mtu *mtu)
{
    if (mtu == nullptr) {
        return -1;
    }
    switch (bytes) {
        case 256U:
            *mtu = IBV_MTU_256;
            return 0;
        case 512U:
            *mtu = IBV_MTU_512;
            return 0;
        case 1024U:
            *mtu = IBV_MTU_1024;
            return 0;
        case 2048U:
            *mtu = IBV_MTU_2048;
            return 0;
        case 4096U:
            *mtu = IBV_MTU_4096;
            return 0;
        default:
            return -1;
    }
}

static ibv_device *find_device(const char *name, nds_device_list *list)
{
    int count = 0;
    int index;

    list->devices = ibv_get_device_list(&count);
    if (list->devices == nullptr) {
        return nullptr;
    }
    for (index = 0; index < count; ++index) {
        if (strcmp(ibv_get_device_name(list->devices[index]), name) == 0) {
            return list->devices[index];
        }
    }
    return nullptr;
}

static int create_resources(struct ibv_device *device, const struct nds_server_config *config,
                            struct nds_verbs_resources *resources)
{
    struct ibv_qp_init_attr qp_init = {};
    resources->context = ibv_open_device(device);
    if (resources->context == nullptr) {
        NDS_LOG_ERROR("cpu-server", "{}: {}", "ibv_open_device", strerror(errno));
        return -1;
    }
    resources->pd = ibv_alloc_pd(resources->context);
    if (resources->pd == nullptr) {
        NDS_LOG_ERROR("cpu-server", "{}: {}", "ibv_alloc_pd", strerror(errno));
        return -1;
    }
    resources->cq = ibv_create_cq(resources->context, (int)(NDS_QP_DEPTH * 2U), nullptr, nullptr, 0);
    if (resources->cq == nullptr) {
        NDS_LOG_ERROR("cpu-server", "{}: {}", "ibv_create_cq", strerror(errno));
        return -1;
    }

    qp_init.send_cq = resources->cq;
    qp_init.recv_cq = resources->cq;
    qp_init.qp_type = IBV_QPT_RC;
    qp_init.cap.max_send_wr = NDS_QP_DEPTH;
    qp_init.cap.max_recv_wr = NDS_QP_DEPTH;
    qp_init.cap.max_send_sge = 1;
    qp_init.cap.max_recv_sge = 1;
    resources->qp = ibv_create_qp(resources->pd, &qp_init);
    if (resources->qp == nullptr) {
        NDS_LOG_ERROR("cpu-server", "{}: {}", "ibv_create_qp", strerror(errno));
        return -1;
    }

    if (ibv_query_gid(resources->context, (uint8_t)config->ib_port, (int)config->gid_index,
                      &resources->gid) != 0) {
        NDS_LOG_ERROR("cpu-server", "{}: {}", "ibv_query_gid", strerror(errno));
        return -1;
    }
    {
        struct ibv_port_attr port_attributes = {};

        if (ibv_query_port(resources->context, (uint8_t)config->ib_port, &port_attributes) != 0) {
            NDS_LOG_ERROR("cpu-server", "{}: {}", "ibv_query_port", strerror(errno));
            return -1;
        }
        if (port_attributes.state != IBV_PORT_ACTIVE) {
            NDS_LOG_ERRORF("cpu-server", "RDMA port %u is not active (state=%u)\n", config->ib_port,
                          (unsigned int)port_attributes.state);
            return -1;
        }
        resources->path_mtu = mtu_to_bytes(port_attributes.active_mtu);
        if (resources->path_mtu == 0U) {
            NDS_LOG_ERRORF("cpu-server", "RDMA port %u reported an unsupported active MTU\n", config->ib_port);
            return -1;
        }
    }
    resources->psn = make_psn();
    return 0;
}

static int create_destination_memory(struct nds_verbs_resources *resources, unsigned int bytes)
{
    const size_t total = (size_t)bytes + (2U * NDS_GUARD_BYTES);

    resources->buffer.resize(total);
    std::fill_n(resources->buffer.begin(), NDS_GUARD_BYTES, NDS_GUARD_VALUE);
    std::fill_n(resources->buffer.begin() + NDS_GUARD_BYTES, bytes, NDS_PAYLOAD_INITIAL_VALUE);
    std::fill_n(resources->buffer.begin() + NDS_GUARD_BYTES + bytes, NDS_GUARD_BYTES, NDS_GUARD_VALUE);
    resources->buffer_length = bytes;
    resources->mr = ibv_reg_mr(resources->pd, resources->buffer.data(), total,
                               IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE);
    if (resources->mr == nullptr) {
        NDS_LOG_ERROR("cpu-server", "{}: {}", "ibv_reg_mr(destination)", strerror(errno));
        resources->buffer.clear();
        resources->buffer_length = 0U;
        return -1;
    }
    return 0;
}

static bool destination_is_valid(const struct nds_verbs_resources *resources,
                                 const unsigned char *expected)
{
    const unsigned char *allocation = resources->buffer.data();
    const size_t bytes = resources->buffer_length;
    size_t index;

    if (resources->buffer.empty() || expected == nullptr || memcmp(allocation + NDS_GUARD_BYTES, expected, bytes) != 0) {
        return false;
    }
    for (index = 0U; index < NDS_GUARD_BYTES; ++index) {
        if (allocation[index] != NDS_GUARD_VALUE ||
            allocation[NDS_GUARD_BYTES + bytes + index] != NDS_GUARD_VALUE) {
            return false;
        }
    }
    return true;
}

static bool wait_for_destination(const struct nds_verbs_resources *resources,
                                 const unsigned char *expected)
{
    const struct timespec delay = {.tv_sec = 0, .tv_nsec = 1000000L};
    unsigned int elapsed;

    for (elapsed = 0U; elapsed < NDS_TRANSFER_WAIT_MS; ++elapsed) {
        if (destination_is_valid(resources, expected)) {
            return true;
        }
        (void)nanosleep(&delay, nullptr);
    }
    return destination_is_valid(resources, expected);
}

static int move_qp_to_init(const struct nds_verbs_resources *resources,
                           const struct nds_server_config *config)
{
    struct ibv_qp_attr attributes = {};

    attributes.qp_state = IBV_QPS_INIT;
    attributes.pkey_index = 0;
    attributes.port_num = (uint8_t)config->ib_port;
    attributes.qp_access_flags = IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ;
    if (ibv_modify_qp(resources->qp, &attributes,
                      IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS) != 0) {
        NDS_LOG_ERROR("cpu-server", "{}: {}", "ibv_modify_qp(INIT)", strerror(errno));
        return -1;
    }
    return 0;
}

static int move_qp_to_rtr_rts(const struct nds_verbs_resources *resources,
                              const struct nds_server_config *config,
                              const nds_rc_endpoint *peer)
{
    struct ibv_qp_attr attributes = {};
    enum ibv_mtu path_mtu;

    /*
     * A verbs QP's RTR path MTU is selected from the local active port, not
     * negotiated by the peer's NDS record. This mirrors HCOMM v9.0.0's
     * RsDrvQpStateModifytoRtr(), which calls RsDrvSetMtu() (local
     * ibv_query_port().active_mtu) and does not consume remote TypicalQp
     * metadata for this setting. TypicalQp itself contains no MTU member.
     *
     * In the direct HCCP path, the NPU-side RA ABI exposes no corresponding
     * MTU field. Treat the peer record as diagnostic only: clamping this CPU
     * QP to an application-configured NPU value can create a QP/wire-MTU
     * mismatch, as the NPU's actual outbound MTU remains runtime-owned.
     */
    const uint32_t local_path_mtu = nds_cpu_qp_path_mtu_select(resources->path_mtu, peer->path_mtu);

    if (local_path_mtu == 0U || mtu_from_bytes(local_path_mtu, &path_mtu) != 0) {
        NDS_LOG_ERRORF("cpu-server", "local port has unsupported path MTU: %u\n", resources->path_mtu);
        return -1;
    }
    if (peer->path_mtu != local_path_mtu) {
        NDS_LOG_INFOF("cpu-server", "CPU uses local active path MTU %u; peer record reports %u (diagnostic only).\n",
                     local_path_mtu, peer->path_mtu);
    }

    attributes.qp_state = IBV_QPS_RTR;
    attributes.path_mtu = path_mtu;
    attributes.dest_qp_num = peer->qp_num;
    attributes.rq_psn = peer->psn;
    attributes.max_dest_rd_atomic = 1;
    attributes.min_rnr_timer = 12;
    attributes.ah_attr.is_global = 1;
    memcpy(&attributes.ah_attr.grh.dgid, peer->gid, NDS_GID_BYTES);
    attributes.ah_attr.grh.sgid_index = (uint8_t)config->gid_index;
    attributes.ah_attr.grh.hop_limit = 1;
    attributes.ah_attr.grh.traffic_class = (uint8_t)peer->traffic_class;
    attributes.ah_attr.sl = (uint8_t)peer->service_level;
    attributes.ah_attr.port_num = (uint8_t)config->ib_port;
    if (ibv_modify_qp(resources->qp, &attributes,
                      IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN | IBV_QP_RQ_PSN |
                          IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER) != 0) {
        NDS_LOG_ERROR("cpu-server", "{}: {}", "ibv_modify_qp(RTR)", strerror(errno));
        return -1;
    }

    attributes = {};
    attributes.qp_state = IBV_QPS_RTS;
    attributes.timeout = 14;
    attributes.retry_cnt = 7;
    attributes.rnr_retry = 7;
    attributes.sq_psn = resources->psn;
    attributes.max_rd_atomic = 1;
    if (ibv_modify_qp(resources->qp, &attributes,
                      IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN |
                          IBV_QP_MAX_QP_RD_ATOMIC) != 0) {
        NDS_LOG_ERROR("cpu-server", "{}: {}", "ibv_modify_qp(RTS)", strerror(errno));
        return -1;
    }
    return 0;
}

static int make_endpoint(const struct nds_verbs_resources *resources,
                         const struct nds_server_config *config,
                         nds_rc_endpoint_wire *wire,
                         char error[NDS_WIRE_ERROR_CAPACITY])
{
    nds_rc_endpoint endpoint = {};
    endpoint.flags = NDS_ENDPOINT_FLAG_QP_ONLY;
    endpoint.qp_num = resources->qp->qp_num;
    endpoint.psn = resources->psn;
    endpoint.port_num = (uint16_t)config->ib_port;
    endpoint.gid_index = (uint16_t)config->gid_index;
    endpoint.path_mtu = resources->path_mtu;
    endpoint.retry_count = 7;
    endpoint.retry_timeout = 14;
    nds_rc_endpoint mutable_endpoint = endpoint;

    memcpy(mutable_endpoint.gid, &resources->gid, NDS_GID_BYTES);
    return nds_rc_endpoint_encode(&mutable_endpoint, wire, error);
}
static const char *qp_state_name(enum ibv_qp_state state)
{
    switch (state) {
        case IBV_QPS_RESET: return "RESET";
        case IBV_QPS_INIT: return "INIT";
        case IBV_QPS_RTR: return "RTR";
        case IBV_QPS_RTS: return "RTS";
        case IBV_QPS_SQD: return "SQD";
        case IBV_QPS_SQE: return "SQE";
        case IBV_QPS_ERR: return "ERR";
        default: return "UNKNOWN";
    }
}

static void format_gid(const uint8_t gid[NDS_GID_BYTES], char text[INET6_ADDRSTRLEN])
{
    if (inet_ntop(AF_INET6, gid, text, INET6_ADDRSTRLEN) == nullptr) {
        (void)snprintf(text, INET6_ADDRSTRLEN, "<invalid>");
    }
}

static int report_qp(const struct nds_verbs_resources *resources, const char *phase)
{
    struct ibv_qp_attr attributes = {};
    struct ibv_qp_init_attr initial = {};
    const int mask = IBV_QP_STATE | IBV_QP_ACCESS_FLAGS | IBV_QP_PKEY_INDEX | IBV_QP_PORT |
                     IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN | IBV_QP_RQ_PSN |
                     IBV_QP_SQ_PSN | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY |
                     IBV_QP_MAX_QP_RD_ATOMIC | IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER;

    if (ibv_query_qp(resources->qp, &attributes, mask, &initial) != 0) {
        NDS_LOG_ERROR("cpu-server", "{}: {}", "ibv_query_qp", strerror(errno));
        return -1;
    }
    NDS_LOG_INFOF("cpu-server", "CPU QP %s: qpn=%u state=%s port=%u pkey_index=%u access=0x%x "
                 "path_mtu=%u dest_qpn=%u rq_psn=%u sq_psn=%u timeout=%u retry=%u rnr_retry=%u "
                 "max_rd_atomic=%u max_dest_rd_atomic=%u min_rnr_timer=%u global=%u sgid_index=%u "
                 "hop_limit=%u tc=%u sl=%u cq_depth=%u max_send_wr=%u max_recv_wr=%u "
                 "max_send_sge=%u max_recv_sge=%u sq_sig_all=%u\n",
                 phase, resources->qp->qp_num, qp_state_name(attributes.qp_state), attributes.port_num,
                 attributes.pkey_index, attributes.qp_access_flags, mtu_to_bytes(attributes.path_mtu),
                 attributes.dest_qp_num, attributes.rq_psn, attributes.sq_psn, attributes.timeout,
                 attributes.retry_cnt, attributes.rnr_retry, attributes.max_rd_atomic,
                 attributes.max_dest_rd_atomic, attributes.min_rnr_timer, attributes.ah_attr.is_global,
                 attributes.ah_attr.grh.sgid_index, attributes.ah_attr.grh.hop_limit,
                 attributes.ah_attr.grh.traffic_class, attributes.ah_attr.sl, NDS_QP_DEPTH * 2U,
                 initial.cap.max_send_wr, initial.cap.max_recv_wr, initial.cap.max_send_sge,
                 initial.cap.max_recv_sge, initial.sq_sig_all);
    return 0;
}

static void hold_for_passive_diagnostics(unsigned int milliseconds)
{
    struct timespec remaining;

    if (milliseconds == 0U) {
        return;
    }
    remaining.tv_sec = (time_t)(milliseconds / 1000U);
    remaining.tv_nsec = (long)(milliseconds % 1000U) * 1000000L;
    NDS_LOG_INFOF("cpu-server", "Holding CPU QP and destination MR for %u ms for passive diagnostics; no additional work is posted.\n",
                 milliseconds);
    while (nanosleep(&remaining, &remaining) != 0 && errno == EINTR) {
    }
}

static void report_endpoint(const char *label, const nds_rc_endpoint *endpoint)
{
    char gid[INET6_ADDRSTRLEN];

    format_gid(endpoint->gid, gid);
    NDS_LOG_INFOF("cpu-server", "%s endpoint: phase=%s qpn=%u psn=%u port=%u gid_index=%u gid=%s path_mtu=%u "
                 "tc=%u sl=%u retry_count=%u retry_timeout=%u\n",
                 label, (endpoint->flags & NDS_ENDPOINT_FLAG_QP_ONLY) != 0U ? "QP-only" : "data-ready",
                 endpoint->qp_num, endpoint->psn, endpoint->port_num, endpoint->gid_index, gid,
                 endpoint->path_mtu, endpoint->traffic_class, endpoint->service_level,
                 endpoint->retry_count, endpoint->retry_timeout);
}

static int wait_for_peer_close(int descriptor)
{
    unsigned char unexpected_byte;

    for (;;) {
        const ssize_t result = read(descriptor, &unexpected_byte, sizeof(unexpected_byte));

        if (result == 0) {
            return 0;
        }
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            NDS_LOG_ERROR("cpu-server", "{}: {}", "read control connection", strerror(errno));
            return -1;
        }
        NDS_LOG_ERRORF("cpu-server", "unexpected control byte after endpoint exchange: 0x%02x\n", unexpected_byte);
        return -1;
    }
}

static int open_listener(const struct nds_server_config *config)
{
    struct sockaddr_in address = {};
    int descriptor;
    int enabled = 1;

    descriptor = socket(AF_INET, SOCK_STREAM, 0);
    if (descriptor < 0) {
        NDS_LOG_ERROR("cpu-server", "{}: {}", "socket", strerror(errno));
        return -1;
    }
    if (setsockopt(descriptor, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled)) != 0) {
        NDS_LOG_ERROR("cpu-server", "{}: {}", "setsockopt(SO_REUSEADDR)", strerror(errno));
        (void)close(descriptor);
        return -1;
    }
    address.sin_family = AF_INET;
    address.sin_port = htons((uint16_t)config->tcp_port);
    if (inet_pton(AF_INET, config->listen_address.c_str(), &address.sin_addr) != 1) {
        NDS_LOG_ERRORF("cpu-server", "invalid IPv4 listen address: %s\n", config->listen_address.c_str());
        (void)close(descriptor);
        return -1;
    }
    if (bind(descriptor, (const struct sockaddr *)&address, sizeof(address)) != 0) {
        NDS_LOG_ERROR("cpu-server", "{}: {}", "bind", strerror(errno));
        (void)close(descriptor);
        return -1;
    }
    if (listen(descriptor, 1) != 0) {
        NDS_LOG_ERROR("cpu-server", "{}: {}", "listen", strerror(errno));
        (void)close(descriptor);
        return -1;
    }
    return descriptor;
}

static int run_server(int argc, char **argv)
{
    struct nds_server_config config;
    struct nds_verbs_resources resources;
    nds_device_list device_list;
    ibv_device *device;
    nds_rc_endpoint_wire peer_wire;
    nds_rc_endpoint_wire local_wire;
    nds_rc_endpoint peer;
    nds_rc_endpoint local;
    nds_memory_descriptor_wire memory_wire;
    nds_memory_descriptor memory;
    nds_transfer_status_wire status_wire;
    nds_transfer_status status;
    std::vector<unsigned char> expected;
    char wire_error[NDS_WIRE_ERROR_CAPACITY] = {};
    nds_file_descriptor listener;
    nds_file_descriptor connection;
    std::string log_error;
    (void)nds::log::configure("cpu-server", "stderr", "info", log_error);
    bool exit_requested = false;
    const int parse_result = parse_arguments(argc, argv, &config, exit_requested);
    if (exit_requested || parse_result != 0) {
        return parse_result;
    }
    if (!nds::log::configure("cpu-server", config.log_sink, config.log_level, log_error)) {
        NDS_LOG_ERROR("cpu-server", "invalid logger configuration: {}", log_error);
        return EXIT_FAILURE;
    }
    device = find_device(config.device_name.c_str(), &device_list);
    if (device == nullptr) {
        NDS_LOG_ERRORF("cpu-server", "RDMA device not found: %s\n", config.device_name.c_str());
        return EXIT_FAILURE;
    }
    if (create_resources(device, &config, &resources) != 0 || move_qp_to_init(&resources, &config) != 0) {
        return EXIT_FAILURE;
    }
    if (!config.qp_only) {
        if (create_destination_memory(&resources, config.bytes) != 0) {
            return EXIT_FAILURE;
        }
        expected.resize(config.bytes);
        for (unsigned int index = 0U; index < config.bytes; ++index) {
            expected[index] = (unsigned char)(index ^ 0x5aU);
        }
    }
    if (make_endpoint(&resources, &config, &local_wire, wire_error) != 0 ||
        nds_rc_endpoint_decode(&local_wire, &local, wire_error) != 0) {
        NDS_LOG_ERRORF("cpu-server", "could not create local QP-only endpoint: %s\n", wire_error);
        return EXIT_FAILURE;
    }
    if (report_qp(&resources, "after INIT") != 0) {
        return EXIT_FAILURE;
    }
    listener.reset(open_listener(&config));
    if (listener.get() < 0) {
        return EXIT_FAILURE;
    }

    NDS_LOG_INFOF("cpu-server", "NDS verbs server ready: device=%s ib_port=%u gid_index=%u tcp=%s:%u mode=%s bytes=%u\n",
                 config.device_name.c_str(), config.ib_port, config.gid_index, config.listen_address.c_str(), config.tcp_port,
                 config.qp_only ? "qp-only" : "rdma-write", config.bytes);
    report_endpoint("CPU local", &local);
    if (!config.qp_only) {
        NDS_LOG_INFOF("cpu-server", "CPU destination MR: addr=%p payload_bytes=%zu lkey=%u rkey=%u\n",
                     resources.buffer.data() + NDS_GUARD_BYTES, resources.buffer_length,
                     resources.mr->lkey, resources.mr->rkey);
    }
    NDS_LOG_INFOF("cpu-server", "Waiting for an 80-byte NDS v2 QP-only peer endpoint description.\n");
    connection.reset(accept(listener.get(), nullptr, nullptr));
    if (connection.get() < 0) {
        NDS_LOG_ERROR("cpu-server", "{}: {}", "accept", strerror(errno));
        return EXIT_FAILURE;
    }
    if (read_full(connection.get(), &peer_wire, sizeof(peer_wire)) != 0 ||
        nds_rc_endpoint_decode(&peer_wire, &peer, wire_error) != 0) {
        NDS_LOG_ERRORF("cpu-server", "invalid or incomplete NDS peer endpoint description: %s\n", wire_error);
        return EXIT_FAILURE;
    }
    if ((peer.flags & NDS_ENDPOINT_FLAG_QP_ONLY) == 0U) {
        NDS_LOG_ERRORF("cpu-server", "CPU QP-only server rejects a data-ready peer endpoint\n");
        return EXIT_FAILURE;
    }
    report_endpoint("NPU remote", &peer);
    /*
     * Do not expose the CPU endpoint until its QP is ready. The NPU receives
     * this reply as its permission to modify/connect its own QP, so replying
     * first would create a needless connection race.
     */
    if (move_qp_to_rtr_rts(&resources, &config, &peer) != 0 || report_qp(&resources, "after RTR/RTS") != 0) {
        return EXIT_FAILURE;
    }
    if (write_full(connection.get(), &local_wire, sizeof(local_wire)) != 0) {
        NDS_LOG_ERROR("cpu-server", "{}: {}", "write endpoint", strerror(errno));
        return EXIT_FAILURE;
    }
    if (config.qp_only) {
        NDS_LOG_INFO("cpu-server", "CPU verbs QP reached RTS; QP-only mode will not advertise memory or expect a work request.");
        if (wait_for_peer_close(connection.get()) != 0) {
            return EXIT_FAILURE;
        }
        NDS_LOG_INFO("cpu-server", "QP-only validation passed; NPU closed the control connection without a work request.");
        return EXIT_SUCCESS;
    }
    memory = {};
    memory.flags = 0U;
    memory.transaction_id = ((uint64_t)resources.qp->qp_num << 32) | resources.psn;
    memory.address = (uint64_t)(uintptr_t)(resources.buffer.data() + NDS_GUARD_BYTES);
    memory.length = resources.buffer_length;
    memory.rkey = resources.mr->rkey;
    memory.access_flags = NDS_MEMORY_ACCESS_REMOTE_WRITE;
    if (nds_memory_descriptor_encode(&memory, &memory_wire, wire_error) != 0 ||
        write_full(connection.get(), &memory_wire, sizeof(memory_wire)) != 0) {
        NDS_LOG_ERRORF("cpu-server", "could not send CPU memory descriptor: %s\n", wire_error);
        return EXIT_FAILURE;
    }
    if (read_full(connection.get(), &status_wire, sizeof(status_wire)) != 0 ||
        nds_transfer_status_decode(&status_wire, &status, wire_error) != 0 ||
        status.status != NDS_TRANSFER_SUBMITTED || status.transaction_id != memory.transaction_id) {
        NDS_LOG_ERRORF("cpu-server", "invalid or incomplete NDS transfer submission: %s\n", wire_error);
        return EXIT_FAILURE;
    }
    status.status = wait_for_destination(&resources, expected.data()) ? NDS_TRANSFER_VERIFIED : NDS_TRANSFER_FAILED;
    if (nds_transfer_status_encode(&status, &status_wire, wire_error) != 0 ||
        write_full(connection.get(), &status_wire, sizeof(status_wire)) != 0) {
        NDS_LOG_ERRORF("cpu-server", "could not send NDS transfer acknowledgment: %s\n", wire_error);
        return EXIT_FAILURE;
    }
    if (report_qp(&resources, "after transfer acknowledgment") != 0) return EXIT_FAILURE;
    hold_for_passive_diagnostics(config.post_close_hold_ms);
    if (status.status != NDS_TRANSFER_VERIFIED) {
        NDS_LOG_ERRORF("cpu-server", "RDMA Write validation failed: payload or guard bytes differ.\n");
        return EXIT_FAILURE;
    }
    NDS_LOG_INFOF("cpu-server", "RDMA Write validation passed: %u bytes matched and both %u-byte guards are intact.\n",
                 config.bytes, NDS_GUARD_BYTES);
    return EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
    return run_server(argc, argv);
}

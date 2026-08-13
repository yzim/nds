#define _POSIX_C_SOURCE 200809L

#include "nds/rdma_path_mtu.h"
#include "nds/peer_exchange.hh"
#include "nds/rdma_wire_codec.h"
#include "nds/logging.hh"
#include "nds/storage_protocol.h"

#include <CLI/CLI.hpp>

#include <arpa/inet.h>
#include <errno.h>
#include <infiniband/verbs.h>
#include <limits.h>
#include <netinet/in.h>
#include <cstdint>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <string>
#include <vector>

#define NDS_DEFAULT_LISTEN "0.0.0.0"
#define NDS_DEFAULT_TCP_PORT 18515U
#define NDS_DEFAULT_IB_PORT 1U
#define NDS_UNSET_GID_INDEX UINT_MAX
#define NDS_QP_DEPTH 16U
#define NDS_TRANSFER_WAIT_MS 5000U

struct nds_server_config {
    std::string device_name;
    std::string listen_address{NDS_DEFAULT_LISTEN};
    unsigned int tcp_port;
    unsigned int ib_port;
    unsigned int gid_index;
    unsigned int namespace_bytes;
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
    nds_storage_command_wire command = {};
    ibv_mr *command_mr = nullptr;
    nds_storage_completion_wire completion = {};
    ibv_mr *completion_mr = nullptr;
    std::vector<unsigned char> namespace_buffer;
    ibv_mr *namespace_mr = nullptr;

    nds_verbs_resources() = default;
    nds_verbs_resources(const nds_verbs_resources &) = delete;
    nds_verbs_resources &operator=(const nds_verbs_resources &) = delete;

    ~nds_verbs_resources()
    {
        if (namespace_mr != nullptr) (void)ibv_dereg_mr(namespace_mr);
        if (completion_mr != nullptr) (void)ibv_dereg_mr(completion_mr);
        if (command_mr != nullptr) (void)ibv_dereg_mr(command_mr);
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
    int release()
    {
        const int released = descriptor_;
        descriptor_ = -1;
        return released;
    }
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
    config->namespace_bytes = 1024U * 1024U;
    config->post_close_hold_ms = 0U;
    CLI::App app{"Serve one-command NDS memory-backed storage requests."};
    app.add_option("--device", config->device_name, "RDMA device name")->required();
    app.add_option("--gid-index", config->gid_index, "RoCE GID index")->required()->check(CLI::Range(0U, static_cast<unsigned int>(INT32_MAX)));
    app.add_option("--listen", config->listen_address, "TCP listen IPv4 address");
    app.add_option("--tcp-port", config->tcp_port, "TCP peer-exchange port")->check(CLI::Range(1U, static_cast<unsigned int>(UINT16_MAX)));
    app.add_option("--ib-port", config->ib_port, "RDMA port")->check(CLI::Range(1U, static_cast<unsigned int>(UINT8_MAX)));
    app.add_option("--namespace-bytes", config->namespace_bytes, "Memory-backed namespace capacity")
        ->check(CLI::Range(1U, 64U * 1024U * 1024U));
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

static int create_storage_memory(struct nds_verbs_resources *resources, unsigned int bytes)
{
    resources->namespace_buffer.resize(bytes, 0U);
    resources->namespace_mr = ibv_reg_mr(resources->pd, resources->namespace_buffer.data(), bytes,
                                         IBV_ACCESS_LOCAL_WRITE);
    if (resources->namespace_mr == nullptr) {
        NDS_LOG_ERROR("cpu-server", "{}: {}", "ibv_reg_mr(namespace)", strerror(errno));
        resources->namespace_buffer.clear();
        return -1;
    }
    resources->completion_mr = ibv_reg_mr(resources->pd, &resources->completion,
                                          sizeof(resources->completion), 0);
    if (resources->completion_mr == nullptr) {
        NDS_LOG_ERROR("cpu-server", "{}: {}", "ibv_reg_mr(completion)", strerror(errno));
        return -1;
    }
    return 0;
}

static int post_command_receive(struct nds_verbs_resources *resources)
{
    ibv_sge sge{};
    ibv_recv_wr receive{};
    ibv_recv_wr *bad = nullptr;
    resources->command_mr = ibv_reg_mr(resources->pd, &resources->command, sizeof(resources->command),
                                        IBV_ACCESS_LOCAL_WRITE);
    if (resources->command_mr == nullptr) {
        NDS_LOG_ERROR("cpu-server", "{}: {}", "ibv_reg_mr(command)", strerror(errno));
        return -1;
    }
    sge.addr = reinterpret_cast<std::uintptr_t>(&resources->command);
    sge.length = sizeof(resources->command);
    sge.lkey = resources->command_mr->lkey;
    receive.wr_id = 1U;
    receive.sg_list = &sge;
    receive.num_sge = 1;
    if (ibv_post_recv(resources->qp, &receive, &bad) != 0) {
        NDS_LOG_ERROR("cpu-server", "{}: {}", "ibv_post_recv(command)", strerror(errno));
        return -1;
    }
    return 0;
}

static int poll_completion(struct nds_verbs_resources *resources, enum ibv_wc_opcode expected_opcode)
{
    const timespec delay{0, 1000000L};
    for (unsigned int elapsed = 0U; elapsed < NDS_TRANSFER_WAIT_MS; ++elapsed) {
        ibv_wc completion{};
        const int count = ibv_poll_cq(resources->cq, 1, &completion);
        if (count < 0 || (count == 1 && (completion.status != IBV_WC_SUCCESS || completion.opcode != expected_opcode))) {
            NDS_LOG_ERRORF("cpu-server", "unexpected CQ completion: count=%d status=%d opcode=%d\n", count,
                           count == 1 ? completion.status : -1, count == 1 ? completion.opcode : -1);
            return -1;
        }
        if (count == 1) return 0;
        (void)nanosleep(&delay, nullptr);
    }
    NDS_LOG_ERROR("cpu-server", "timed out waiting for verbs CQ completion");
    return -1;
}

static int post_storage_operation(struct nds_verbs_resources *resources, const nds_storage_command *command,
                                  const nds_storage_bootstrap *bootstrap)
{
    ibv_sge data_sge{};
    ibv_sge completion_sge{};
    ibv_send_wr data{};
    ibv_send_wr completion{};
    ibv_send_wr *bad = nullptr;
    nds_storage_completion response{command->request_id, NDS_STORAGE_COMPLETION_COMPLETE, NDS_STORAGE_SUCCESS, command->length};
    char error[NDS_STORAGE_ERROR_CAPACITY]{};

    if (command->offset > resources->namespace_buffer.size() || command->length > resources->namespace_buffer.size() - command->offset) {
        response.status = NDS_STORAGE_RANGE_ERROR;
        response.bytes_transferred = 0U;
    }
    if (nds_storage_completion_encode(&response, &resources->completion, error) != 0) {
        NDS_LOG_ERRORF("cpu-server", "cannot encode completion: %s\n", error);
        return -1;
    }
    completion_sge.addr = reinterpret_cast<std::uintptr_t>(&resources->completion);
    completion_sge.length = sizeof(nds_storage_completion_wire);
    completion_sge.lkey = resources->completion_mr->lkey;
    completion.wr_id = 3U;
    completion.sg_list = &completion_sge;
    completion.num_sge = 1;
    completion.opcode = IBV_WR_RDMA_WRITE;
    completion.send_flags = IBV_SEND_SIGNALED;
    completion.wr.rdma.remote_addr = bootstrap->completion.address;
    completion.wr.rdma.rkey = bootstrap->completion.rkey;
    if (response.status == NDS_STORAGE_SUCCESS) {
        data_sge.addr = reinterpret_cast<std::uintptr_t>(resources->namespace_buffer.data() + command->offset);
        data_sge.length = static_cast<uint32_t>(command->length);
        data_sge.lkey = resources->namespace_mr->lkey;
        data.wr_id = 2U;
        data.sg_list = &data_sge;
        data.num_sge = 1;
        data.opcode = command->operation == NDS_STORAGE_READ ? IBV_WR_RDMA_WRITE : IBV_WR_RDMA_READ;
        data.wr.rdma.remote_addr = command->data.address;
        data.wr.rdma.rkey = command->data.rkey;
        data.next = &completion;
        if (ibv_post_send(resources->qp, &data, &bad) != 0) {
            NDS_LOG_ERROR("cpu-server", "{}: {}", "ibv_post_send(storage data)", strerror(errno));
            return -1;
        }
    } else if (ibv_post_send(resources->qp, &completion, &bad) != 0) {
        NDS_LOG_ERROR("cpu-server", "{}: {}", "ibv_post_send(range completion)", strerror(errno));
        return -1;
    }
    return poll_completion(resources, IBV_WC_RDMA_WRITE);
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
    NDS_LOG_INFOF("cpu-server", "Holding CPU QP and storage MRs for %u ms for passive diagnostics; no additional work is posted.\n",
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
    nds_storage_bootstrap bootstrap{};
    nds_storage_namespace storage_namespace{};
    nds_storage_command command{};
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
    if (!config.qp_only && create_storage_memory(&resources, config.namespace_bytes) != 0) return EXIT_FAILURE;
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

    NDS_LOG_INFOF("cpu-server", "NDS verbs server ready: device=%s ib_port=%u gid_index=%u tcp=%s:%u mode=%s namespace_bytes=%u\n",
                 config.device_name.c_str(), config.ib_port, config.gid_index, config.listen_address.c_str(), config.tcp_port,
                 config.qp_only ? "qp-only" : "storage", config.namespace_bytes);
    report_endpoint("CPU local", &local);
    if (!config.qp_only) {
        NDS_LOG_INFOF("cpu-server", "CPU memory-backed namespace: bytes=%zu lkey=%u rkey=%u\n",
                      resources.namespace_buffer.size(), resources.namespace_mr->lkey, resources.namespace_mr->rkey);
    }
    NDS_LOG_INFOF("cpu-server", "Waiting for an NDS QP endpoint description.\n");
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
    if (!config.qp_only && post_command_receive(&resources) != 0) return EXIT_FAILURE;
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
    nds::TcpPeerExchange exchange(connection.release());
    std::string exchange_error;
    if (!exchange.receive_storage_bootstrap(bootstrap, &exchange_error)) {
        NDS_LOG_ERROR_LINE("cpu-server") << "NPU storage bootstrap failed: " << exchange_error << '\n';
        return EXIT_FAILURE;
    }
    storage_namespace.capacity = resources.namespace_buffer.size();
    if (!exchange.send_storage_namespace(storage_namespace, &exchange_error)) {
        NDS_LOG_ERROR_LINE("cpu-server") << "CPU namespace bootstrap failed: " << exchange_error << '\n';
        return EXIT_FAILURE;
    }
    if (poll_completion(&resources, IBV_WC_RECV) != 0 ||
        nds_storage_command_decode(&resources.command, &command, wire_error) != 0) {
        NDS_LOG_ERRORF("cpu-server", "invalid NDS storage command: %s\n", wire_error);
        return EXIT_FAILURE;
    }
    if (post_storage_operation(&resources, &command, &bootstrap) != 0) return EXIT_FAILURE;
    if (report_qp(&resources, "after storage completion") != 0) return EXIT_FAILURE;
    hold_for_passive_diagnostics(config.post_close_hold_ms);
    NDS_LOG_INFOF("cpu-server", "completed NDS storage command request_id=%llu operation=%u bytes=%llu\n",
                  (unsigned long long)command.request_id, command.operation, (unsigned long long)command.length);
    return EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
    return run_server(argc, argv);
}

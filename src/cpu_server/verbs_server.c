#define _POSIX_C_SOURCE 200809L

#include "nds/rdma_wire_codec.h"

#include <arpa/inet.h>
#include <errno.h>
#include <infiniband/verbs.h>
#include <limits.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define NDS_DEFAULT_LISTEN "0.0.0.0"
#define NDS_DEFAULT_TCP_PORT 18515U
#define NDS_DEFAULT_IB_PORT 1U
#define NDS_UNSET_GID_INDEX UINT_MAX
#define NDS_QP_DEPTH 16U

struct nds_server_config {
    const char *device_name;
    const char *listen_address;
    unsigned int tcp_port;
    unsigned int ib_port;
    unsigned int gid_index;
};

struct nds_verbs_resources {
    struct ibv_context *context;
    struct ibv_pd *pd;
    struct ibv_cq *cq;
    struct ibv_qp *qp;
    uint32_t psn;
    uint32_t path_mtu;
    union ibv_gid gid;
};

static void usage(const char *program)
{
    (void)fprintf(stderr,
                  "usage: %s --device NAME --gid-index INDEX [--listen IPV4] [--tcp-port PORT] "
                  "[--ib-port PORT] [--bytes BYTES]\n",
                  program);
}

static int parse_unsigned(const char *text, unsigned int *value)
{
    char *end = NULL;
    unsigned long parsed;

    errno = 0;
    parsed = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || parsed > UINT32_MAX) {
        return -1;
    }
    *value = (unsigned int)parsed;
    return 0;
}

static int parse_arguments(int argc, char **argv, struct nds_server_config *config)
{
    int index;

    *config = (struct nds_server_config){
        .device_name = NULL,
        .listen_address = NDS_DEFAULT_LISTEN,
        .tcp_port = NDS_DEFAULT_TCP_PORT,
        .ib_port = NDS_DEFAULT_IB_PORT,
        .gid_index = NDS_UNSET_GID_INDEX,
    };

    for (index = 1; index < argc; index += 2) {
        if (index + 1 >= argc) {
            return -1;
        }
        if (strcmp(argv[index], "--device") == 0) {
            config->device_name = argv[index + 1];
        } else if (strcmp(argv[index], "--listen") == 0) {
            config->listen_address = argv[index + 1];
        } else if (strcmp(argv[index], "--tcp-port") == 0) {
            if (parse_unsigned(argv[index + 1], &config->tcp_port) != 0 ||
                config->tcp_port == 0 || config->tcp_port > UINT16_MAX) {
                return -1;
            }
        } else if (strcmp(argv[index], "--ib-port") == 0) {
            if (parse_unsigned(argv[index + 1], &config->ib_port) != 0 ||
                config->ib_port == 0 || config->ib_port > UINT8_MAX) {
                return -1;
            }
        } else if (strcmp(argv[index], "--gid-index") == 0) {
            if (parse_unsigned(argv[index + 1], &config->gid_index) != 0 ||
                config->gid_index > INT32_MAX) {
                return -1;
            }
        } else {
            return -1;
        }
    }
    if (config->device_name == NULL || config->gid_index == NDS_UNSET_GID_INDEX) {
        return -1;
    }
    return 0;
}

static int read_full(int descriptor, void *buffer, size_t length)
{
    unsigned char *cursor = buffer;

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
    const unsigned char *cursor = buffer;

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
    if (mtu == NULL) {
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

static struct ibv_device *find_device(const char *name, struct ibv_device ***list)
{
    int count = 0;
    int index;

    *list = ibv_get_device_list(&count);
    if (*list == NULL) {
        return NULL;
    }
    for (index = 0; index < count; ++index) {
        if (strcmp(ibv_get_device_name((*list)[index]), name) == 0) {
            return (*list)[index];
        }
    }
    return NULL;
}

static void destroy_resources(struct nds_verbs_resources *resources)
{
    if (resources->qp != NULL) {
        (void)ibv_destroy_qp(resources->qp);
    }
    if (resources->cq != NULL) {
        (void)ibv_destroy_cq(resources->cq);
    }
    if (resources->pd != NULL) {
        (void)ibv_dealloc_pd(resources->pd);
    }
    if (resources->context != NULL) {
        (void)ibv_close_device(resources->context);
    }
    *resources = (struct nds_verbs_resources){0};
}

static int create_resources(struct ibv_device *device, const struct nds_server_config *config,
                            struct nds_verbs_resources *resources)
{
    struct ibv_qp_init_attr qp_init = {0};
    resources->context = ibv_open_device(device);
    if (resources->context == NULL) {
        perror("ibv_open_device");
        return -1;
    }
    resources->pd = ibv_alloc_pd(resources->context);
    if (resources->pd == NULL) {
        perror("ibv_alloc_pd");
        return -1;
    }
    resources->cq = ibv_create_cq(resources->context, (int)(NDS_QP_DEPTH * 2U), NULL, NULL, 0);
    if (resources->cq == NULL) {
        perror("ibv_create_cq");
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
    if (resources->qp == NULL) {
        perror("ibv_create_qp");
        return -1;
    }

    if (ibv_query_gid(resources->context, (uint8_t)config->ib_port, (int)config->gid_index,
                      &resources->gid) != 0) {
        perror("ibv_query_gid");
        return -1;
    }
    {
        struct ibv_port_attr port_attributes = {0};

        if (ibv_query_port(resources->context, (uint8_t)config->ib_port, &port_attributes) != 0) {
            perror("ibv_query_port");
            return -1;
        }
        if (port_attributes.state != IBV_PORT_ACTIVE) {
            (void)fprintf(stderr, "RDMA port %u is not active (state=%u)\n", config->ib_port,
                          (unsigned int)port_attributes.state);
            return -1;
        }
        resources->path_mtu = mtu_to_bytes(port_attributes.active_mtu);
        if (resources->path_mtu == 0U) {
            (void)fprintf(stderr, "RDMA port %u reported an unsupported active MTU\n", config->ib_port);
            return -1;
        }
    }
    resources->psn = make_psn();
    return 0;
}

static int move_qp_to_init(const struct nds_verbs_resources *resources,
                           const struct nds_server_config *config)
{
    struct ibv_qp_attr attributes = {0};

    attributes.qp_state = IBV_QPS_INIT;
    attributes.pkey_index = 0;
    attributes.port_num = (uint8_t)config->ib_port;
    attributes.qp_access_flags = IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ;
    if (ibv_modify_qp(resources->qp, &attributes,
                      IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS) != 0) {
        perror("ibv_modify_qp(INIT)");
        return -1;
    }
    return 0;
}

static int move_qp_to_rtr_rts(const struct nds_verbs_resources *resources,
                              const struct nds_server_config *config,
                              const nds_rc_endpoint *peer)
{
    struct ibv_qp_attr attributes = {0};
    enum ibv_mtu path_mtu;

    if (mtu_from_bytes(peer->path_mtu, &path_mtu) != 0) {
        (void)fprintf(stderr, "peer advertised unsupported path MTU: %u\n", peer->path_mtu);
        return -1;
    }
    if (peer->path_mtu > resources->path_mtu) {
        if (mtu_from_bytes(resources->path_mtu, &path_mtu) != 0) {
            (void)fprintf(stderr, "local port has unsupported path MTU: %u\n", resources->path_mtu);
            return -1;
        }
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
        perror("ibv_modify_qp(RTR)");
        return -1;
    }

    attributes = (struct ibv_qp_attr){0};
    attributes.qp_state = IBV_QPS_RTS;
    attributes.timeout = 14;
    attributes.retry_cnt = 7;
    attributes.rnr_retry = 7;
    attributes.sq_psn = resources->psn;
    attributes.max_rd_atomic = 1;
    if (ibv_modify_qp(resources->qp, &attributes,
                      IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN |
                          IBV_QP_MAX_QP_RD_ATOMIC) != 0) {
        perror("ibv_modify_qp(RTS)");
        return -1;
    }
    return 0;
}

static int make_endpoint(const struct nds_verbs_resources *resources,
                         const struct nds_server_config *config,
                         nds_rc_endpoint_wire_v1 *wire,
                         char error[NDS_WIRE_ERROR_CAPACITY])
{
    const nds_rc_endpoint endpoint = {
        .flags = NDS_ENDPOINT_FLAG_QP_ONLY,
        .qp_num = resources->qp->qp_num,
        .psn = resources->psn,
        .port_num = (uint16_t)config->ib_port,
        .gid_index = (uint16_t)config->gid_index,
        .path_mtu = resources->path_mtu,
        .access_flags = 0,
        .traffic_class = 0,
        .service_level = 0,
        .retry_count = 7,
        .retry_timeout = 14,
            };
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
    if (inet_ntop(AF_INET6, gid, text, INET6_ADDRSTRLEN) == NULL) {
        (void)snprintf(text, INET6_ADDRSTRLEN, "<invalid>");
    }
}

static int report_qp(const struct nds_verbs_resources *resources, const char *phase)
{
    struct ibv_qp_attr attributes = {0};
    struct ibv_qp_init_attr initial = {0};
    const int mask = IBV_QP_STATE | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN | IBV_QP_RQ_PSN | IBV_QP_SQ_PSN;

    if (ibv_query_qp(resources->qp, &attributes, mask, &initial) != 0) {
        perror("ibv_query_qp");
        return -1;
    }
    (void)printf("CPU QP %s: qpn=%u state=%s cq_depth=%u max_send_wr=%u max_recv_wr=%u "
                 "max_send_sge=%u max_recv_sge=%u path_mtu=%u dest_qpn=%u rq_psn=%u sq_psn=%u\n",
                 phase, resources->qp->qp_num, qp_state_name(attributes.qp_state), NDS_QP_DEPTH * 2U,
                 initial.cap.max_send_wr, initial.cap.max_recv_wr, initial.cap.max_send_sge,
                 initial.cap.max_recv_sge, mtu_to_bytes(attributes.path_mtu), attributes.dest_qp_num,
                 attributes.rq_psn, attributes.sq_psn);
    return 0;
}

static void report_endpoint(const char *label, const nds_rc_endpoint *endpoint)
{
    char gid[INET6_ADDRSTRLEN];

    format_gid(endpoint->gid, gid);
    (void)printf("%s endpoint: phase=%s qpn=%u psn=%u port=%u gid_index=%u gid=%s path_mtu=%u "
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
            perror("read control connection");
            return -1;
        }
        (void)fprintf(stderr, "unexpected control byte after endpoint exchange: 0x%02x\n", unexpected_byte);
        return -1;
    }
}

static int open_listener(const struct nds_server_config *config)
{
    struct sockaddr_in address = {0};
    int descriptor;
    int enabled = 1;

    descriptor = socket(AF_INET, SOCK_STREAM, 0);
    if (descriptor < 0) {
        perror("socket");
        return -1;
    }
    if (setsockopt(descriptor, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled)) != 0) {
        perror("setsockopt(SO_REUSEADDR)");
        (void)close(descriptor);
        return -1;
    }
    address.sin_family = AF_INET;
    address.sin_port = htons((uint16_t)config->tcp_port);
    if (inet_pton(AF_INET, config->listen_address, &address.sin_addr) != 1) {
        (void)fprintf(stderr, "invalid IPv4 listen address: %s\n", config->listen_address);
        (void)close(descriptor);
        return -1;
    }
    if (bind(descriptor, (const struct sockaddr *)&address, sizeof(address)) != 0) {
        perror("bind");
        (void)close(descriptor);
        return -1;
    }
    if (listen(descriptor, 1) != 0) {
        perror("listen");
        (void)close(descriptor);
        return -1;
    }
    return descriptor;
}

int main(int argc, char **argv)
{
    struct nds_server_config config;
    struct nds_verbs_resources resources = {0};
    struct ibv_device **device_list = NULL;
    struct ibv_device *device;
    nds_rc_endpoint_wire_v1 peer_wire;
    nds_rc_endpoint_wire_v1 local_wire;
    nds_rc_endpoint peer;
    nds_rc_endpoint local;
    char wire_error[NDS_WIRE_ERROR_CAPACITY] = {0};
    int listener = -1;
    int connection = -1;
    int exit_code = EXIT_FAILURE;

    if (parse_arguments(argc, argv, &config) != 0) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }
    device = find_device(config.device_name, &device_list);
    if (device == NULL) {
        (void)fprintf(stderr, "RDMA device not found: %s\n", config.device_name);
        goto out;
    }
    if (create_resources(device, &config, &resources) != 0 || move_qp_to_init(&resources, &config) != 0) {
        goto out;
    }
    if (make_endpoint(&resources, &config, &local_wire, wire_error) != 0 ||
        nds_rc_endpoint_decode(&local_wire, &local, wire_error) != 0) {
        (void)fprintf(stderr, "could not create local QP-only endpoint: %s\n", wire_error);
        goto out;
    }
    if (report_qp(&resources, "after INIT") != 0) {
        goto out;
    }
    listener = open_listener(&config);
    if (listener < 0) {
        goto out;
    }

    (void)printf("NDS QP-only verbs server ready: device=%s ib_port=%u gid_index=%u tcp=%s:%u\n",
                 config.device_name, config.ib_port, config.gid_index, config.listen_address, config.tcp_port);
    report_endpoint("CPU local", &local);
    (void)puts("No memory region is registered and no work request will be posted in this QP-only milestone.");
    (void)printf("Waiting for an 80-byte NDS v2 QP-only peer endpoint description.\n");
    connection = accept(listener, NULL, NULL);
    if (connection < 0) {
        perror("accept");
        goto out;
    }
    if (read_full(connection, &peer_wire, sizeof(peer_wire)) != 0 ||
        nds_rc_endpoint_decode(&peer_wire, &peer, wire_error) != 0) {
        (void)fprintf(stderr, "invalid or incomplete NDS peer endpoint description: %s\n", wire_error);
        goto out;
    }
    if ((peer.flags & NDS_ENDPOINT_FLAG_QP_ONLY) == 0U) {
        (void)fprintf(stderr, "CPU QP-only server rejects a data-ready peer endpoint\n");
        goto out;
    }
    report_endpoint("NPU remote", &peer);
    /*
     * Do not expose the CPU endpoint until its QP is ready. The NPU receives
     * this reply as its permission to modify/connect its own QP, so replying
     * first would create a needless connection race.
     */
    if (move_qp_to_rtr_rts(&resources, &config, &peer) != 0 || report_qp(&resources, "after RTR/RTS") != 0) {
        goto out;
    }
    if (write_full(connection, &local_wire, sizeof(local_wire)) != 0) {
        perror("write endpoint");
        goto out;
    }
    (void)puts("CPU verbs QP reached RTS. Holding the QP until the NPU reports RaTypicalQpModify success and closes TCP.");
    if (wait_for_peer_close(connection) != 0) {
        goto out;
    }
    exit_code = EXIT_SUCCESS;

out:
    if (connection >= 0) {
        (void)close(connection);
    }
    if (listener >= 0) {
        (void)close(listener);
    }
    destroy_resources(&resources);
    if (device_list != NULL) {
        ibv_free_device_list(device_list);
    }
    return exit_code;
}

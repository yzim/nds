#include "nds/rdma_wire_codec.h"

#include <arpa/inet.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static void nds_wire_set_error(char error[NDS_WIRE_ERROR_CAPACITY], const char *format, ...) {
    va_list arguments;

    if (error == nullptr) {
        return;
    }
    va_start(arguments, format);
    (void)vsnprintf(error, NDS_WIRE_ERROR_CAPACITY, format, arguments);
    va_end(arguments);
}

static int nds_rc_endpoint_validate(const nds_rc_endpoint *endpoint, char error[NDS_WIRE_ERROR_CAPACITY]) {
    if (endpoint == nullptr) {
        nds_wire_set_error(error, "endpoint is required");
        return -1;
    }
    if (endpoint->qp_num == 0U || endpoint->qp_num > UINT32_C(0x00ffffff)) {
        nds_wire_set_error(error, "QP number must be a non-zero 24-bit value");
        return -1;
    }
    if (endpoint->psn > UINT32_C(0x00ffffff)) {
        nds_wire_set_error(error, "PSN must be a 24-bit value");
        return -1;
    }
    if (endpoint->port_num == 0U) {
        nds_wire_set_error(error, "RDMA port must be non-zero");
        return -1;
    }
    if (endpoint->path_mtu == 0U) {
        nds_wire_set_error(error, "path MTU must be non-zero");
        return -1;
    }
    if (endpoint->traffic_class > UINT8_MAX) {
        nds_wire_set_error(error, "traffic class exceeds 8 bits");
        return -1;
    }
    if (endpoint->service_level > 15U) {
        nds_wire_set_error(error, "service level exceeds 4 bits");
        return -1;
    }
    if (endpoint->retry_count > 7U || endpoint->retry_timeout > 31U) {
        nds_wire_set_error(error, "retry values exceed RC QP limits");
        return -1;
    }
    return 0;
}

int nds_rc_endpoint_encode(const nds_rc_endpoint *endpoint, nds_rc_endpoint_wire *wire,
                           char error[NDS_WIRE_ERROR_CAPACITY]) {
    if (wire == nullptr) {
        nds_wire_set_error(error, "wire record is required");
        return -1;
    }
    if (nds_rc_endpoint_validate(endpoint, error) != 0) {
        return -1;
    }

    *wire = {};
    wire->magic = htonl(NDS_RC_WIRE_MAGIC);
    wire->version = htons(NDS_RC_WIRE_VERSION);
    wire->qp_num = htonl(endpoint->qp_num);
    wire->psn = htonl(endpoint->psn);
    wire->port_num = htons(endpoint->port_num);
    wire->gid_index = htons(endpoint->gid_index);
    wire->path_mtu = htonl(endpoint->path_mtu);
    wire->traffic_class = htonl(endpoint->traffic_class);
    wire->service_level = htonl(endpoint->service_level);
    wire->retry_count = htonl(endpoint->retry_count);
    wire->retry_timeout = htonl(endpoint->retry_timeout);
    memcpy(wire->gid, endpoint->gid, sizeof(wire->gid));
    if (error != nullptr) {
        error[0] = '\0';
    }
    return 0;
}

int nds_rc_endpoint_decode(const nds_rc_endpoint_wire *wire, nds_rc_endpoint *endpoint,
                           char error[NDS_WIRE_ERROR_CAPACITY]) {
    nds_rc_endpoint decoded;

    if (wire == nullptr || endpoint == nullptr) {
        nds_wire_set_error(error, "wire record and endpoint output are required");
        return -1;
    }
    if (ntohl(wire->magic) != NDS_RC_WIRE_MAGIC) {
        nds_wire_set_error(error, "unexpected NDS wire magic");
        return -1;
    }
    if (ntohs(wire->version) != NDS_RC_WIRE_VERSION) {
        nds_wire_set_error(error, "unsupported NDS wire version: %u", ntohs(wire->version));
        return -1;
    }

    decoded = {};
    decoded.qp_num = ntohl(wire->qp_num);
    decoded.psn = ntohl(wire->psn);
    decoded.port_num = ntohs(wire->port_num);
    decoded.gid_index = ntohs(wire->gid_index);
    decoded.path_mtu = ntohl(wire->path_mtu);
    decoded.traffic_class = ntohl(wire->traffic_class);
    decoded.service_level = ntohl(wire->service_level);
    decoded.retry_count = ntohl(wire->retry_count);
    decoded.retry_timeout = ntohl(wire->retry_timeout);
    memcpy(decoded.gid, wire->gid, sizeof(decoded.gid));
    if (nds_rc_endpoint_validate(&decoded, error) != 0) {
        return -1;
    }
    *endpoint = decoded;
    if (error != nullptr) {
        error[0] = '\0';
    }
    return 0;
}

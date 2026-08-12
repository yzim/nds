#include "nds/rdma_wire_codec.h"

#include <arpa/inet.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static uint64_t nds_host_to_be64(uint64_t value)
{
    const uint32_t high = htonl((uint32_t)(value >> 32));
    const uint32_t low = htonl((uint32_t)value);
    return ((uint64_t)low << 32) | high;
}

static uint64_t nds_be64_to_host(uint64_t value)
{
    const uint32_t high = ntohl((uint32_t)(value >> 32));
    const uint32_t low = ntohl((uint32_t)value);
    return ((uint64_t)low << 32) | high;
}

static void nds_wire_set_error(char error[NDS_WIRE_ERROR_CAPACITY], const char *format, ...)
{
    va_list arguments;

    if (error == NULL) {
        return;
    }
    va_start(arguments, format);
    (void)vsnprintf(error, NDS_WIRE_ERROR_CAPACITY, format, arguments);
    va_end(arguments);
}

static int nds_rc_endpoint_validate(const nds_rc_endpoint *endpoint,
                                    char error[NDS_WIRE_ERROR_CAPACITY])
{
    if (endpoint == NULL) {
        nds_wire_set_error(error, "endpoint is required");
        return -1;
    }
    if ((endpoint->flags & ~NDS_ENDPOINT_FLAG_ALL) != 0U ||
        endpoint->flags == 0U ||
        (endpoint->flags & NDS_ENDPOINT_FLAG_ALL) == NDS_ENDPOINT_FLAG_ALL) {
        nds_wire_set_error(error, "endpoint must specify exactly one known phase flag");
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
    if ((endpoint->flags & NDS_ENDPOINT_FLAG_QP_ONLY) != 0U) {
        if (endpoint->rkey != 0U || endpoint->address != 0U || endpoint->access_flags != 0U) {
            nds_wire_set_error(error, "QP-only endpoint must not advertise memory metadata");
            return -1;
        }
    } else if (endpoint->rkey == 0U || endpoint->address == 0U || endpoint->access_flags == 0U) {
        nds_wire_set_error(error, "data-ready endpoint requires address, rkey, and access flags");
        return -1;
    }
    return 0;
}

int nds_rc_endpoint_encode(const nds_rc_endpoint *endpoint, nds_rc_endpoint_wire_v1 *wire,
                           char error[NDS_WIRE_ERROR_CAPACITY])
{
    if (wire == NULL) {
        nds_wire_set_error(error, "wire record is required");
        return -1;
    }
    if (nds_rc_endpoint_validate(endpoint, error) != 0) {
        return -1;
    }

    *wire = {};
    wire->magic = htonl(NDS_RC_WIRE_MAGIC);
    wire->version = htons(NDS_RC_WIRE_VERSION);
    wire->flags = htons(endpoint->flags);
    wire->qp_num = htonl(endpoint->qp_num);
    wire->psn = htonl(endpoint->psn);
    wire->rkey = htonl(endpoint->rkey);
    wire->port_num = htons(endpoint->port_num);
    wire->gid_index = htons(endpoint->gid_index);
    wire->path_mtu = htonl(endpoint->path_mtu);
    wire->access_flags = htonl(endpoint->access_flags);
    wire->traffic_class = htonl(endpoint->traffic_class);
    wire->service_level = htonl(endpoint->service_level);
    wire->retry_count = htonl(endpoint->retry_count);
    wire->retry_timeout = htonl(endpoint->retry_timeout);
    wire->address = nds_host_to_be64(endpoint->address);
    memcpy(wire->gid, endpoint->gid, sizeof(wire->gid));
    if (error != NULL) {
        error[0] = '\0';
    }
    return 0;
}

int nds_rc_endpoint_decode(const nds_rc_endpoint_wire_v1 *wire, nds_rc_endpoint *endpoint,
                           char error[NDS_WIRE_ERROR_CAPACITY])
{
    nds_rc_endpoint decoded;

    if (wire == NULL || endpoint == NULL) {
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
    decoded.flags = ntohs(wire->flags);
    decoded.qp_num = ntohl(wire->qp_num);
    decoded.psn = ntohl(wire->psn);
    decoded.rkey = ntohl(wire->rkey);
    decoded.port_num = ntohs(wire->port_num);
    decoded.gid_index = ntohs(wire->gid_index);
    decoded.path_mtu = ntohl(wire->path_mtu);
    decoded.access_flags = ntohl(wire->access_flags);
    decoded.traffic_class = ntohl(wire->traffic_class);
    decoded.service_level = ntohl(wire->service_level);
    decoded.retry_count = ntohl(wire->retry_count);
    decoded.retry_timeout = ntohl(wire->retry_timeout);
    decoded.address = nds_be64_to_host(wire->address);
    memcpy(decoded.gid, wire->gid, sizeof(decoded.gid));
    if (nds_rc_endpoint_validate(&decoded, error) != 0) {
        return -1;
    }
    *endpoint = decoded;
    if (error != NULL) {
        error[0] = '\0';
    }
    return 0;
}


static int nds_memory_descriptor_validate(const nds_memory_descriptor *descriptor,
                                          char error[NDS_WIRE_ERROR_CAPACITY])
{
    if (descriptor == NULL) {
        nds_wire_set_error(error, "memory descriptor is required");
        return -1;
    }
    if (descriptor->transaction_id == 0U || descriptor->address == 0U || descriptor->length == 0U ||
        descriptor->rkey == 0U ||
        (descriptor->access_flags & NDS_MEMORY_ACCESS_REMOTE_WRITE) == 0U) {
        nds_wire_set_error(error, "memory descriptor requires transaction ID, address, length, rkey, and remote-write access");
        return -1;
    }
    return 0;
}

int nds_memory_descriptor_encode(const nds_memory_descriptor *descriptor,
                                 nds_memory_descriptor_wire_v1 *wire,
                                 char error[NDS_WIRE_ERROR_CAPACITY])
{
    if (wire == NULL) {
        nds_wire_set_error(error, "memory descriptor wire record is required");
        return -1;
    }
    if (nds_memory_descriptor_validate(descriptor, error) != 0) {
        return -1;
    }
    *wire = {};
    wire->magic = htonl(NDS_MEMORY_WIRE_MAGIC);
    wire->version = htons(NDS_MEMORY_WIRE_VERSION);
    wire->flags = htons(descriptor->flags);
    wire->transaction_id = nds_host_to_be64(descriptor->transaction_id);
    wire->address = nds_host_to_be64(descriptor->address);
    wire->length = nds_host_to_be64(descriptor->length);
    wire->rkey = htonl(descriptor->rkey);
    wire->access_flags = htonl(descriptor->access_flags);
    return 0;
}

int nds_memory_descriptor_decode(const nds_memory_descriptor_wire_v1 *wire,
                                 nds_memory_descriptor *descriptor,
                                 char error[NDS_WIRE_ERROR_CAPACITY])
{
    if (wire == NULL || descriptor == NULL) {
        nds_wire_set_error(error, "memory descriptor wire record and output are required");
        return -1;
    }
    if (ntohl(wire->magic) != NDS_MEMORY_WIRE_MAGIC || ntohs(wire->version) != NDS_MEMORY_WIRE_VERSION) {
        nds_wire_set_error(error, "unsupported memory descriptor magic or version");
        return -1;
    }
    *descriptor = {};
    descriptor->flags = ntohs(wire->flags);
    descriptor->transaction_id = nds_be64_to_host(wire->transaction_id);
    descriptor->address = nds_be64_to_host(wire->address);
    descriptor->length = nds_be64_to_host(wire->length);
    descriptor->rkey = ntohl(wire->rkey);
    descriptor->access_flags = ntohl(wire->access_flags);
    return nds_memory_descriptor_validate(descriptor, error);
}

static int nds_transfer_status_validate(const nds_transfer_status *status,
                                        char error[NDS_WIRE_ERROR_CAPACITY])
{
    if (status == NULL || status->transaction_id == 0U ||
        (status->status != NDS_TRANSFER_SUBMITTED && status->status != NDS_TRANSFER_VERIFIED &&
         status->status != NDS_TRANSFER_FAILED)) {
        nds_wire_set_error(error, "invalid transfer status");
        return -1;
    }
    return 0;
}

int nds_transfer_status_encode(const nds_transfer_status *status,
                               nds_transfer_status_wire_v1 *wire,
                               char error[NDS_WIRE_ERROR_CAPACITY])
{
    if (wire == NULL || nds_transfer_status_validate(status, error) != 0) return -1;
    *wire = {};
    wire->magic = htonl(NDS_TRANSFER_STATUS_WIRE_MAGIC);
    wire->version = htons(NDS_TRANSFER_STATUS_WIRE_VERSION);
    wire->status = htons(status->status);
    wire->transaction_id = nds_host_to_be64(status->transaction_id);
    return 0;
}

int nds_transfer_status_decode(const nds_transfer_status_wire_v1 *wire,
                               nds_transfer_status *status,
                               char error[NDS_WIRE_ERROR_CAPACITY])
{
    static const uint8_t zero_reserved[8] = {0};
    if (wire == NULL || status == NULL) {
        nds_wire_set_error(error, "transfer status wire record and output are required");
        return -1;
    }
    if (ntohl(wire->magic) != NDS_TRANSFER_STATUS_WIRE_MAGIC ||
        ntohs(wire->version) != NDS_TRANSFER_STATUS_WIRE_VERSION ||
        memcmp(wire->reserved, zero_reserved, sizeof(wire->reserved)) != 0) {
        nds_wire_set_error(error, "invalid transfer status header");
        return -1;
    }
    *status = {};
    status->status = ntohs(wire->status);
    status->transaction_id = nds_be64_to_host(wire->transaction_id);
    return nds_transfer_status_validate(status, error);
}

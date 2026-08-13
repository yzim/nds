#include "nds/transport.h"
#include "nds/protocol.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>

static int expect(int condition, const char *message) {
    if (condition == 0) {
        (void)fprintf(stderr, "FAIL: %s\n", message);
        return -1;
    }
    return 0;
}

int main(void) {
    nds_transport_endpoint source = {
        .qp_num = UINT32_C(0x00abcd),
        .psn = UINT32_C(0x00123456),
        .port_num = 1,
        .gid_index = 3,
        .path_mtu = 4096,
        .traffic_class = 106,
        .service_level = 5,
        .retry_count = 7,
        .retry_timeout = 14,
        .gid = {0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88},
    };
    nds_transport_endpoint decoded = {};
    nds_transport_endpoint_wire wire = {};
    char error[NDS_TRANSPORT_ERROR_CAPACITY] = {};

    if (nds_transport_endpoint_encode(&source, &wire, error) != 0 ||
        expect(ntohl(wire.magic) == NDS_TRANSPORT_WIRE_MAGIC, "wire magic") != 0 ||
        expect(ntohs(wire.version) == NDS_TRANSPORT_WIRE_VERSION, "wire version") != 0 ||
        expect(ntohl(wire.qp_num) == source.qp_num, "QP number encoding") != 0 ||
        expect(memcmp(wire.gid, source.gid, NDS_GID_BYTES) == 0, "GID encoding") != 0 ||
        nds_transport_endpoint_decode(&wire, &decoded, error) != 0 ||
        expect(memcmp(&source, &decoded, sizeof(source)) == 0, "round trip") != 0) {
        (void)fprintf(stderr, "codec error: %s\n", error);
        return 1;
    }

    {
        const nds_protocol_bootstrap bootstrap_source = {
            .completion = {.address = UINT64_C(0x0102030405060708),
                           .length = 64U,
                           .rkey = UINT32_C(0x12345678),
                           .access = NDS_PROTOCOL_ACCESS_REMOTE_WRITE},
        };
        nds_protocol_bootstrap bootstrap_decoded = {};
        nds_protocol_bootstrap_wire bootstrap_wire = {};
        if (nds_protocol_bootstrap_encode(&bootstrap_source, &bootstrap_wire, error) != 0 ||
            nds_protocol_bootstrap_decode(&bootstrap_wire, &bootstrap_decoded, error) != 0 ||
            expect(memcmp(&bootstrap_source, &bootstrap_decoded, sizeof(bootstrap_source)) == 0,
                   "storage bootstrap round trip") != 0) {
            (void)fprintf(stderr, "storage bootstrap codec error: %s\n", error);
            return 1;
        }
    }

    {
        const nds_protocol_namespace namespace_source = {.capacity = 1024U * 1024U};
        nds_protocol_namespace namespace_decoded = {};
        nds_protocol_namespace_wire namespace_wire = {};
        if (nds_protocol_namespace_encode(&namespace_source, &namespace_wire, error) != 0 ||
            nds_protocol_namespace_decode(&namespace_wire, &namespace_decoded, error) != 0 ||
            expect(namespace_decoded.capacity == namespace_source.capacity, "storage namespace round trip") != 0) {
            (void)fprintf(stderr, "storage namespace codec error: %s\n", error);
            return 1;
        }
    }

    {
        const nds_protocol_command read_source = {
            .request_id = UINT64_C(0x1020304050607080),
            .operation = NDS_PROTOCOL_READ,
            .offset = 4096U,
            .length = 8192U,
            .data = {.address = UINT64_C(0x1020304050607080),
                     .length = 8192U,
                     .rkey = UINT32_C(0x12345678),
                     .access = NDS_PROTOCOL_ACCESS_REMOTE_WRITE},
        };
        nds_protocol_command decoded_command = {};
        nds_protocol_command_wire command_wire = {};
        if (nds_protocol_command_encode(&read_source, &command_wire, error) != 0 ||
            nds_protocol_command_decode(&command_wire, &decoded_command, error) != 0 ||
            expect(memcmp(&read_source, &decoded_command, sizeof(read_source)) == 0,
                   "storage Read command round trip") != 0) {
            (void)fprintf(stderr, "storage command codec error: %s\n", error);
            return 1;
        }
        command_wire.data_access = htonl(NDS_PROTOCOL_ACCESS_REMOTE_READ);
        if (expect(nds_protocol_command_decode(&command_wire, &decoded_command, error) != 0,
                   "storage Read command requires remote-write access") != 0) {
            return 1;
        }
    }

    {
        const nds_protocol_completion completion_source = {
            .request_id = UINT64_C(0x1020304050607080),
            .state = NDS_PROTOCOL_COMPLETION_COMPLETE,
            .status = NDS_PROTOCOL_SUCCESS,
            .bytes_transferred = 8192U,
        };
        nds_protocol_completion decoded_completion = {};
        nds_protocol_completion_wire completion_wire = {};
        if (nds_protocol_completion_encode(&completion_source, &completion_wire, error) != 0 ||
            nds_protocol_completion_decode(&completion_wire, &decoded_completion, error) != 0 ||
            expect(memcmp(&completion_source, &decoded_completion, sizeof(completion_source)) == 0,
                   "storage completion round trip") != 0) {
            (void)fprintf(stderr, "storage completion codec error: %s\n", error);
            return 1;
        }
    }

    wire.magic = 0;
    if (expect(nds_transport_endpoint_decode(&wire, &decoded, error) != 0, "bad magic rejected") != 0) {
        return 1;
    }
    (void)puts("transport endpoint codec tests passed");
    return 0;
}

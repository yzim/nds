#include "nds/rdma_wire_codec.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>

static int expect(int condition, const char *message)
{
    if (condition == 0) {
        (void)fprintf(stderr, "FAIL: %s\n", message);
        return -1;
    }
    return 0;
}

int main(void)
{
    nds_rc_endpoint source = {
        .flags = NDS_ENDPOINT_FLAG_DATA_READY,
        .qp_num = UINT32_C(0x00abcd),
        .psn = UINT32_C(0x00123456),
        .rkey = UINT32_C(0x87654321),
        .port_num = 1,
        .gid_index = 3,
        .path_mtu = 4096,
        .access_flags = UINT32_C(0x1234),
        .traffic_class = 106,
        .service_level = 5,
        .retry_count = 7,
        .retry_timeout = 14,
        .address = UINT64_C(0x0123456789abcdef),
        .gid = {0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88},
    };
    nds_rc_endpoint decoded = {0};
    nds_rc_endpoint_wire_v1 wire = {0};
    char error[NDS_WIRE_ERROR_CAPACITY] = {0};

    if (nds_rc_endpoint_encode(&source, &wire, error) != 0 ||
        expect(ntohl(wire.magic) == NDS_RC_WIRE_MAGIC, "wire magic") != 0 ||
        expect(ntohs(wire.version) == NDS_RC_WIRE_VERSION, "wire version") != 0 ||
        expect(ntohl(wire.qp_num) == source.qp_num, "QP number encoding") != 0 ||
        expect(memcmp(wire.gid, source.gid, NDS_GID_BYTES) == 0, "GID encoding") != 0 ||
        nds_rc_endpoint_decode(&wire, &decoded, error) != 0 ||
        expect(memcmp(&source, &decoded, sizeof(source)) == 0, "round trip") != 0) {
        (void)fprintf(stderr, "codec error: %s\n", error);
        return 1;
    }


    source.flags = NDS_ENDPOINT_FLAG_QP_ONLY;
    source.rkey = 0;
    source.address = 0;
    source.access_flags = 0;
    if (nds_rc_endpoint_encode(&source, &wire, error) != 0 ||
        nds_rc_endpoint_decode(&wire, &decoded, error) != 0 ||
        expect(decoded.flags == NDS_ENDPOINT_FLAG_QP_ONLY, "QP-only phase round trip") != 0 ||
        expect(decoded.rkey == 0U && decoded.address == 0U && decoded.access_flags == 0U,
               "QP-only has no memory metadata") != 0) {
        (void)fprintf(stderr, "QP-only codec error: %s\n", error);
        return 1;
    }
    source.rkey = 1;
    if (expect(nds_rc_endpoint_encode(&source, &wire, error) != 0,
               "QP-only endpoint with rkey rejected") != 0) {
        return 1;
    }

    wire.magic = 0;
    if (expect(nds_rc_endpoint_decode(&wire, &decoded, error) != 0, "bad magic rejected") != 0) {
        return 1;
    }
    (void)puts("rdma wire codec tests passed");
    return 0;
}

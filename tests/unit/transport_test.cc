#include "nds/protocol.h"
#include "nds/transport.h"

#include <arpa/inet.h>

#include <cstdio>
#include <cstring>

namespace {

int expect(bool condition, const char *message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        return 1;
    }
    return 0;
}

nds_transport_endpoint endpoint() {
    nds_transport_endpoint value{};
    value.qp_num = 0x00abcdU;
    value.psn = 0x00123456U;
    value.port_num = 1U;
    value.gid_index = 3U;
    value.path_mtu = 4096U;
    value.traffic_class = 106U;
    value.service_level = 5U;
    value.retry_count = 7U;
    value.retry_timeout = 14U;
    value.gid[0] = 0xfeU;
    value.gid[1] = 0x80U;
    return value;
}

}  // namespace

int main() {
    const nds_transport_endpoint source = endpoint();
    nds_transport_endpoint decoded{};
    nds_transport_endpoint_wire wire{};
    if (nds_transport_endpoint_encode(&source, &wire) != NDS_TRANSPORT_RESULT_OK ||
        expect(ntohl(wire.magic) == NDS_TRANSPORT_WIRE_MAGIC, "transport magic") != 0 ||
        expect(ntohs(wire.version) == NDS_TRANSPORT_WIRE_VERSION, "transport version") != 0 ||
        expect(nds_transport_endpoint_decode(&wire, &decoded) == NDS_TRANSPORT_RESULT_OK, "transport decode") != 0 ||
        expect(std::memcmp(&source, &decoded, sizeof(source)) == 0, "transport round trip") != 0) {
        return 1;
    }

    nds_protocol_bootstrap bootstrap_source{{0x0102030405060708U, 64U, 0x12345678U, NDS_PROTOCOL_ACCESS_REMOTE_WRITE}};
    nds_protocol_bootstrap bootstrap_decoded{};
    nds_protocol_bootstrap_wire bootstrap_wire{};
    if (nds_protocol_bootstrap_encode(&bootstrap_source, &bootstrap_wire) != NDS_PROTOCOL_RESULT_OK ||
        nds_protocol_bootstrap_decode(&bootstrap_wire, &bootstrap_decoded) != NDS_PROTOCOL_RESULT_OK ||
        expect(std::memcmp(&bootstrap_source, &bootstrap_decoded, sizeof(bootstrap_source)) == 0,
               "bootstrap round trip") != 0) {
        return 1;
    }

    nds_protocol_namespace namespace_source{1024U * 1024U};
    nds_protocol_namespace namespace_decoded{};
    nds_protocol_namespace_wire namespace_wire{};
    if (nds_protocol_namespace_encode(&namespace_source, &namespace_wire) != NDS_PROTOCOL_RESULT_OK ||
        nds_protocol_namespace_decode(&namespace_wire, &namespace_decoded) != NDS_PROTOCOL_RESULT_OK ||
        expect(namespace_source.capacity == namespace_decoded.capacity, "namespace round trip") != 0) {
        return 1;
    }

    nds_protocol_command command_source{0x1020304050607080U,
                                        NDS_PROTOCOL_READ,
                                        4096U,
                                        8192U,
                                        {0x1020304050607080U, 8192U, 0x12345678U, NDS_PROTOCOL_ACCESS_REMOTE_WRITE}};
    nds_protocol_command command_decoded{};
    nds_protocol_command_wire command_wire{};
    if (nds_protocol_command_encode(&command_source, &command_wire) != NDS_PROTOCOL_RESULT_OK ||
        nds_protocol_command_decode(&command_wire, &command_decoded) != NDS_PROTOCOL_RESULT_OK ||
        expect(std::memcmp(&command_source, &command_decoded, sizeof(command_source)) == 0, "command round trip") !=
            0) {
        return 1;
    }
    command_wire.data_access = htonl(NDS_PROTOCOL_ACCESS_REMOTE_READ);
    if (expect(nds_protocol_command_decode(&command_wire, &command_decoded) != NDS_PROTOCOL_RESULT_OK,
               "invalid command access rejected") != 0) {
        return 1;
    }

    nds_protocol_completion completion_source{0x1020304050607080U, NDS_PROTOCOL_COMPLETION_COMPLETE,
                                              NDS_PROTOCOL_SUCCESS, 8192U};
    nds_protocol_completion completion_decoded{};
    nds_protocol_completion_wire completion_wire{};
    if (nds_protocol_completion_encode(&completion_source, &completion_wire) != NDS_PROTOCOL_RESULT_OK ||
        nds_protocol_completion_decode(&completion_wire, &completion_decoded) != NDS_PROTOCOL_RESULT_OK ||
        expect(std::memcmp(&completion_source, &completion_decoded, sizeof(completion_source)) == 0,
               "completion round trip") != 0) {
        return 1;
    }

    wire.magic = 0U;
    return expect(nds_transport_endpoint_decode(&wire, &decoded) != NDS_TRANSPORT_RESULT_OK,
                  "invalid transport magic rejected");
}

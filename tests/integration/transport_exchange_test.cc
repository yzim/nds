#include "nds/connection.hh"

#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <future>
#include <iostream>

namespace {

nds_qp_info make_endpoint(std::uint32_t qpn, std::uint32_t psn) {
    nds_qp_info endpoint{};
    endpoint.qp_num = qpn;
    endpoint.psn = psn;
    endpoint.port_num = 1;
    endpoint.gid_index = 3;
    endpoint.path_mtu = 4096;
    endpoint.traffic_class = 0;
    endpoint.service_level = 0;
    endpoint.retry_count = 7;
    endpoint.retry_timeout = 14;
    endpoint.gid[0] = 0xfe;
    endpoint.gid[1] = 0x80;
    return endpoint;
}

}  // namespace

int main() {
    int sockets[2]{};
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) != 0) {
        std::perror("socketpair");
        return 1;
    }
    const auto client = make_endpoint(0x1101, 0x2202);
    const auto server = make_endpoint(0x3303, 0x4404);
    std::future<nds::Result<nds_qp_info>> server_result = std::async(
        std::launch::async, [fd = sockets[1], server] { return nds::TcpPeerExchange{fd}.exchange_as_server(server); });
    const auto client_result = nds::TcpPeerExchange{sockets[0]}.exchange_as_client(client);
    const auto remote_result = server_result.get();

    if (!client_result || !remote_result || client_result->qp_num != server.qp_num ||
        remote_result->qp_num != client.qp_num || client_result->psn != server.psn ||
        remote_result->psn != client.psn) {
        std::cerr << "peer exchange failed\n";
        return 1;
    }
    std::cout << "peer exchange test passed\n";
    return 0;
}

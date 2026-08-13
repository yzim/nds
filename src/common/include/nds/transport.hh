#ifndef NDS_TRANSPORT_HH
#define NDS_TRANSPORT_HH

#include "nds/transport.h"
#include "nds/result.hh"

#include <cstdint>
#include <string>

namespace nds {

/*
 * Project-owned TCP peer exchange for endpoint negotiation. It transfers
 * exactly one fixed-size NDS transport endpoint record in each direction. It
 * deliberately contains no HCCP, HCOMM, ACL, or verbs dependency.
 */
class TcpPeerExchange {
public:
    explicit TcpPeerExchange(int fd = -1) noexcept;
    ~TcpPeerExchange();
    TcpPeerExchange(const TcpPeerExchange &) = delete;
    TcpPeerExchange &operator=(const TcpPeerExchange &) = delete;

    /* Open a TCP connection without exchanging. The returned object owns the socket. */
    static Result<void> connect(const std::string &ipv4, std::uint16_t port, std::uint32_t timeout_ms,
                                TcpPeerExchange *connection);

    /* Client ordering: send the local endpoint, then receive the peer endpoint. */
    Result<nds_transport_endpoint> exchange_as_client(const nds_transport_endpoint &local) const;

    /* Server ordering: receive the peer endpoint, then send the local endpoint. */
    Result<nds_transport_endpoint> exchange_as_server(const nds_transport_endpoint &local) const;

    Result<void> send_bytes(const void *buffer, std::size_t length) const;
    Result<void> receive_bytes(void *buffer, std::size_t length) const;

    Result<void> adopt(int fd);

private:
    static Result<void> read_full(int fd, void *buffer, std::size_t length);
    static Result<void> write_full(int fd, const void *buffer, std::size_t length);
    static Result<nds_transport_endpoint> exchange(int fd, const nds_transport_endpoint &local, bool client_order);

    int fd_;
};

}  // namespace nds

#endif

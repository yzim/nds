#ifndef NDS_TRANSPORT_HH
#define NDS_TRANSPORT_HH

#include "nds/transport.h"

#include <cstdint>
#include <string>

namespace nds {

struct PeerExchangeResult {
    bool ok{false};
    nds_transport_endpoint peer{};
    std::string error;
};

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
    static bool connect(const std::string &ipv4, std::uint16_t port, std::uint32_t timeout_ms,
                        TcpPeerExchange *connection, std::string *error);

    /* Client ordering: send the local endpoint, then receive the peer endpoint. */
    PeerExchangeResult exchange_as_client(const nds_transport_endpoint &local) const;

    /* Server ordering: receive the peer endpoint, then send the local endpoint. */
    PeerExchangeResult exchange_as_server(const nds_transport_endpoint &local) const;

    bool send_bytes(const void *buffer, std::size_t length, std::string *error) const;
    bool receive_bytes(void *buffer, std::size_t length, std::string *error) const;

    bool adopt(int fd, std::string *error);

private:
    static bool read_full(int fd, void *buffer, std::size_t length, std::string *error);
    static bool write_full(int fd, const void *buffer, std::size_t length, std::string *error);
    static PeerExchangeResult exchange(int fd, const nds_transport_endpoint &local, bool client_order);

    int fd_;
};

}  // namespace nds

#endif

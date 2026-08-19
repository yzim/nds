#ifndef NDS_CONNECTION_HH
#define NDS_CONNECTION_HH

#include "nds/connection.h"
#include "nds/result.hh"

#include <cstdint>
#include <string>

namespace nds {

/*
 * TCP helper used by connection control to exchange one nds_qp_info in each
 * direction. It has no HCCP, HCOMM, ACL, or verbs dependency.
 */
class TcpPeerExchange {
public:
    explicit TcpPeerExchange(int fd = -1) noexcept;
    ~TcpPeerExchange();
    TcpPeerExchange(const TcpPeerExchange &) = delete;
    TcpPeerExchange &operator=(const TcpPeerExchange &) = delete;
    TcpPeerExchange(TcpPeerExchange &&other) noexcept;
    TcpPeerExchange &operator=(TcpPeerExchange &&other) noexcept;

    /* Open a TCP connection without exchanging. The returned object owns the socket. */
    static Result<TcpPeerExchange> connect(const std::string &ipv4, std::uint16_t port, std::uint32_t timeout_ms);

    /* Client ordering: send the local QP info, then receive the peer QP info. */
    Result<nds_qp_info> exchange_as_client(const nds_qp_info &local) const;

    /* Server ordering: receive the peer QP info, then send the local QP info. */
    Result<nds_qp_info> exchange_as_server(const nds_qp_info &local) const;

    Result<void> send_bytes(const void *buffer, std::size_t length) const;
    Result<void> receive_bytes(void *buffer, std::size_t length) const;

    Result<void> adopt(int fd);

private:
    static Result<void> read_full(int fd, void *buffer, std::size_t length);
    static Result<void> write_full(int fd, const void *buffer, std::size_t length);
    static Result<nds_qp_info> exchange(int fd, const nds_qp_info &local, bool client_order);

    int fd_;
};

}  // namespace nds

#endif

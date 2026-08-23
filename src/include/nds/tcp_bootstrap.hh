#ifndef NDS_TCP_BOOTSTRAP_HH
#define NDS_TCP_BOOTSTRAP_HH

#include "nds/wire/transport.hh"
#include "nds/result.hh"

#include <cstdint>
#include <string>
#include <vector>

namespace nds {

struct TcpAddress {
    std::string ipv4;
    std::uint16_t port{};
};

Result<TcpAddress> parse_tcp_address(const std::string &address);

/*
 * TCP helper used by transport control to exchange one QP-info record in each
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
    Result<transport::QpInfo> exchange_as_client(const transport::QpInfo &local) const;

    /* Server ordering: receive the peer QP info, then send the local QP info. */
    Result<transport::QpInfo> exchange_as_server(const transport::QpInfo &local) const;

    /* Batch ordering is the same, but all QP records share this one socket. */
    Result<std::vector<transport::QpInfo>> exchange_as_client(
        const std::vector<transport::QpInfo> &local) const;
    Result<std::vector<transport::QpInfo>> exchange_as_server(
        const std::vector<transport::QpInfo> &local) const;

    Result<void> send_bytes(const void *buffer, std::size_t length) const;
    Result<void> receive_bytes(void *buffer, std::size_t length) const;

    Result<void> adopt(int fd);

private:
    static Result<void> read_full(int fd, void *buffer, std::size_t length);
    static Result<void> write_full(int fd, const void *buffer, std::size_t length);
    static Result<transport::QpInfo> exchange(int fd, const transport::QpInfo &local, bool client_order);
    static Result<std::vector<transport::QpInfo>> exchange_many(
        int fd, const std::vector<transport::QpInfo> &local, bool client_order);

    int fd_;
};

}  // namespace nds

#endif

#ifndef NDS_TCP_SOCKET_HH
#define NDS_TCP_SOCKET_HH

#include "result.hh"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace nds {

struct TcpAddress {
    std::string ipv4;
    std::uint16_t port{};
};

Result<TcpAddress> parse_tcp_address(const std::string &address);

/* Owns one connected TCP byte channel. Callers define framing, records, and
 * ordering above it. */
class TcpConnection {
public:
    explicit TcpConnection(int fd = -1) noexcept;
    ~TcpConnection();
    TcpConnection(const TcpConnection &) = delete;
    TcpConnection &operator=(const TcpConnection &) = delete;
    TcpConnection(TcpConnection &&other) noexcept;
    TcpConnection &operator=(TcpConnection &&other) noexcept;

    static Result<TcpConnection> connect(const std::string &ipv4, std::uint16_t port, std::uint32_t timeout_ms);

    Result<void> send(std::span<const std::byte> bytes) const;
    Result<void> receive(std::span<std::byte> bytes) const;

private:
    static Result<void> read_full(int fd, void *buffer, std::size_t length);
    static Result<void> write_full(int fd, const void *buffer, std::size_t length);

    int fd_{-1};
};

/* Owns one TCP listener and accepts independent client connections. */
class TcpListener {
public:
    explicit TcpListener(int fd = -1) noexcept;
    ~TcpListener();
    TcpListener(const TcpListener &) = delete;
    TcpListener &operator=(const TcpListener &) = delete;
    TcpListener(TcpListener &&other) noexcept;
    TcpListener &operator=(TcpListener &&other) noexcept;

    static Result<TcpListener> listen(const std::string &ipv4, std::uint16_t port, int backlog);

    bool is_open() const noexcept;
    Result<TcpConnection> accept() const;

private:
    int fd_{-1};
};

}  // namespace nds

#endif

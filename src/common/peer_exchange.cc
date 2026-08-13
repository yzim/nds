#include "nds/peer_exchange.hh"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <utility>

namespace nds {
namespace {

std::string system_error(const char *operation)
{
    return std::string(operation) + ": " + std::strerror(errno);
}

bool wait_for_fd(int fd, short events, std::uint32_t timeout_ms, std::string *error)
{
    pollfd descriptor{};
    descriptor.fd = fd;
    descriptor.events = events;
    const int result = poll(&descriptor, 1, static_cast<int>(timeout_ms));

    if (result == 0) {
        *error = "TCP peer exchange timeout";
        return false;
    }
    if (result < 0) {
        *error = system_error("poll");
        return false;
    }
    if ((descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
        *error = "TCP peer exchange socket became unavailable";
        return false;
    }
    return true;
}

} // namespace

TcpPeerExchange::TcpPeerExchange(int fd) noexcept : fd_(fd) {}

TcpPeerExchange::~TcpPeerExchange()
{
    if (fd_ >= 0) {
        (void)close(fd_);
    }
}

TcpPeerExchange::TcpPeerExchange(TcpPeerExchange &&other) noexcept : fd_(other.release()) {}

TcpPeerExchange &TcpPeerExchange::operator=(TcpPeerExchange &&other) noexcept
{
    if (this != &other) {
        if (fd_ >= 0) {
            (void)close(fd_);
        }
        fd_ = other.release();
    }
    return *this;
}

bool TcpPeerExchange::send_storage_bootstrap(const nds_storage_bootstrap &bootstrap, std::string *error) const
{
    nds_storage_bootstrap_wire wire{};
    char codec_error[NDS_STORAGE_ERROR_CAPACITY]{};
    if (fd_ < 0 || nds_storage_bootstrap_encode(&bootstrap, &wire, codec_error) != 0) {
        if (error != nullptr) *error = fd_ < 0 ? "TCP peer exchange is not connected" : codec_error;
        return false;
    }
    return write_full(fd_, &wire, sizeof(wire), error);
}

bool TcpPeerExchange::receive_storage_bootstrap(nds_storage_bootstrap &bootstrap, std::string *error) const
{
    nds_storage_bootstrap_wire wire{};
    char codec_error[NDS_STORAGE_ERROR_CAPACITY]{};
    if (fd_ < 0 || !read_full(fd_, &wire, sizeof(wire), error) ||
        nds_storage_bootstrap_decode(&wire, &bootstrap, codec_error) != 0) {
        if (fd_ >= 0 && error != nullptr && codec_error[0] != '\0') *error = codec_error;
        return false;
    }
    return true;
}

bool TcpPeerExchange::send_storage_namespace(const nds_storage_namespace &storage_namespace, std::string *error) const
{
    nds_storage_namespace_wire wire{};
    char codec_error[NDS_STORAGE_ERROR_CAPACITY]{};
    if (fd_ < 0 || nds_storage_namespace_encode(&storage_namespace, &wire, codec_error) != 0) {
        if (error != nullptr) *error = fd_ < 0 ? "TCP peer exchange is not connected" : codec_error;
        return false;
    }
    return write_full(fd_, &wire, sizeof(wire), error);
}

bool TcpPeerExchange::receive_storage_namespace(nds_storage_namespace &storage_namespace, std::string *error) const
{
    nds_storage_namespace_wire wire{};
    char codec_error[NDS_STORAGE_ERROR_CAPACITY]{};
    if (fd_ < 0 || !read_full(fd_, &wire, sizeof(wire), error) ||
        nds_storage_namespace_decode(&wire, &storage_namespace, codec_error) != 0) {
        if (fd_ >= 0 && error != nullptr && codec_error[0] != '\0') *error = codec_error;
        return false;
    }
    return true;
}

int TcpPeerExchange::fd() const noexcept
{
    return fd_;
}

int TcpPeerExchange::release() noexcept
{
    const int released = fd_;
    fd_ = -1;
    return released;
}

bool TcpPeerExchange::read_full(int fd, void *buffer, std::size_t length, std::string *error)
{
    auto *cursor = static_cast<unsigned char *>(buffer);

    while (length != 0U) {
        const ssize_t result = read(fd, cursor, length);
        if (result == 0) {
            *error = "peer closed TCP peer exchange connection";
            return false;
        }
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            *error = system_error("read");
            return false;
        }
        cursor += static_cast<std::size_t>(result);
        length -= static_cast<std::size_t>(result);
    }
    return true;
}

bool TcpPeerExchange::write_full(int fd, const void *buffer, std::size_t length, std::string *error)
{
    const auto *cursor = static_cast<const unsigned char *>(buffer);

    while (length != 0U) {
        const ssize_t result = write(fd, cursor, length);
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            *error = system_error("write");
            return false;
        }
        cursor += static_cast<std::size_t>(result);
        length -= static_cast<std::size_t>(result);
    }
    return true;
}

PeerExchangeResult TcpPeerExchange::exchange(int fd, const nds_rc_endpoint &local, bool client_order)
{
    nds_rc_endpoint_wire local_wire{};
    nds_rc_endpoint_wire peer_wire{};
    PeerExchangeResult result;
    char codec_error[NDS_WIRE_ERROR_CAPACITY]{};

    if (fd < 0) {
        result.error = "TCP peer exchange socket is not open";
        return result;
    }
    if (nds_rc_endpoint_encode(&local, &local_wire, codec_error) != 0) {
        result.error = std::string("cannot encode local endpoint: ") + codec_error;
        return result;
    }

    if (client_order) {
        if (!write_full(fd, &local_wire, sizeof(local_wire), &result.error) ||
            !read_full(fd, &peer_wire, sizeof(peer_wire), &result.error)) {
            return result;
        }
    } else {
        if (!read_full(fd, &peer_wire, sizeof(peer_wire), &result.error) ||
            !write_full(fd, &local_wire, sizeof(local_wire), &result.error)) {
            return result;
        }
    }
    if (nds_rc_endpoint_decode(&peer_wire, &result.peer, codec_error) != 0) {
        result.error = std::string("cannot decode peer endpoint: ") + codec_error;
        return result;
    }
    result.ok = true;
    return result;
}

PeerExchangeResult TcpPeerExchange::exchange_as_client(const nds_rc_endpoint &local) const
{
    return exchange(fd_, local, true);
}

PeerExchangeResult TcpPeerExchange::exchange_as_server(const nds_rc_endpoint &local) const
{
    return exchange(fd_, local, false);
}

bool TcpPeerExchange::connect(const std::string &ipv4, std::uint16_t port, std::uint32_t timeout_ms,
                               TcpPeerExchange &connection, std::string *error)
{
    sockaddr_in address{};
    int socket_fd;
    int flags;
    int connect_result;

    if (connection.fd_ >= 0) {
        if (error != nullptr) {
            *error = "output TCP peer exchange object already owns a socket";
        }
        return false;
    }
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (inet_pton(AF_INET, ipv4.c_str(), &address.sin_addr) != 1) {
        if (error != nullptr) {
            *error = "invalid TCP peer IPv4 address: " + ipv4;
        }
        return false;
    }
    socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        if (error != nullptr) {
            *error = system_error("socket");
        }
        return false;
    }
    flags = fcntl(socket_fd, F_GETFL, 0);
    if (flags < 0 || fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK) != 0) {
        if (error != nullptr) {
            *error = system_error("fcntl");
        }
        (void)close(socket_fd);
        return false;
    }
    connect_result = ::connect(socket_fd, reinterpret_cast<const sockaddr *>(&address), sizeof(address));
    if (connect_result != 0 && errno != EINPROGRESS) {
        if (error != nullptr) {
            *error = system_error("connect");
        }
        (void)close(socket_fd);
        return false;
    }
    if (connect_result != 0 && !wait_for_fd(socket_fd, POLLOUT, timeout_ms, error)) {
        (void)close(socket_fd);
        return false;
    }
    if (connect_result != 0) {
        int socket_error = 0;
        socklen_t socket_error_length = sizeof(socket_error);

        if (getsockopt(socket_fd, SOL_SOCKET, SO_ERROR, &socket_error, &socket_error_length) != 0 ||
            socket_error != 0) {
            if (error != nullptr) {
                if (socket_error != 0) {
                    *error = std::string("connect: ") + std::strerror(socket_error);
                } else {
                    *error = system_error("getsockopt");
                }
            }
            (void)close(socket_fd);
            return false;
        }
    }
    if (fcntl(socket_fd, F_SETFL, flags) != 0) {
        if (error != nullptr) {
            *error = system_error("fcntl");
        }
        (void)close(socket_fd);
        return false;
    }
    connection.fd_ = socket_fd;
    return true;
}

PeerExchangeResult TcpPeerExchange::connect_and_exchange(const std::string &ipv4, std::uint16_t port,
                                                         const nds_rc_endpoint &local,
                                                         std::uint32_t timeout_ms)
{
    TcpPeerExchange connection;
    std::string error;

    if (!connect(ipv4, port, timeout_ms, connection, &error)) {
        PeerExchangeResult result;
        result.error = std::move(error);
        return result;
    }
    return connection.exchange_as_client(local);
}

} // namespace nds

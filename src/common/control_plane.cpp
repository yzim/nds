#include "nds/control_plane.hpp"

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
        *error = "TCP control-plane timeout";
        return false;
    }
    if (result < 0) {
        *error = system_error("poll");
        return false;
    }
    if ((descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
        *error = "TCP control-plane socket became unavailable";
        return false;
    }
    return true;
}

} // namespace

TcpControlPlane::TcpControlPlane(int fd) noexcept : fd_(fd) {}

TcpControlPlane::~TcpControlPlane()
{
    if (fd_ >= 0) {
        (void)close(fd_);
    }
}

TcpControlPlane::TcpControlPlane(TcpControlPlane &&other) noexcept : fd_(other.release()) {}

TcpControlPlane &TcpControlPlane::operator=(TcpControlPlane &&other) noexcept
{
    if (this != &other) {
        if (fd_ >= 0) {
            (void)close(fd_);
        }
        fd_ = other.release();
    }
    return *this;
}

bool TcpControlPlane::send_memory_descriptor(const nds_memory_descriptor &descriptor, std::string *error) const
{
    nds_memory_descriptor_wire_v1 wire{};
    char codec_error[NDS_WIRE_ERROR_CAPACITY]{};

    if (fd_ < 0) {
        if (error != nullptr) *error = "control plane is not connected";
        return false;
    }
    if (nds_memory_descriptor_encode(&descriptor, &wire, codec_error) != 0) {
        if (error != nullptr) *error = std::string("cannot encode memory descriptor: ") + codec_error;
        return false;
    }
    return write_full(fd_, &wire, sizeof(wire), error);
}

bool TcpControlPlane::receive_memory_descriptor(nds_memory_descriptor &descriptor, std::string *error) const
{
    nds_memory_descriptor_wire_v1 wire{};
    char codec_error[NDS_WIRE_ERROR_CAPACITY]{};

    if (fd_ < 0) {
        if (error != nullptr) *error = "control plane is not connected";
        return false;
    }
    if (!read_full(fd_, &wire, sizeof(wire), error)) return false;
    if (nds_memory_descriptor_decode(&wire, &descriptor, codec_error) != 0) {
        if (error != nullptr) *error = std::string("cannot decode memory descriptor: ") + codec_error;
        return false;
    }
    return true;
}

bool TcpControlPlane::send_transfer_status(const nds_transfer_status &status, std::string *error) const
{
    nds_transfer_status_wire_v1 wire{};
    char codec_error[NDS_WIRE_ERROR_CAPACITY]{};
    if (fd_ < 0) {
        if (error != nullptr) *error = "control plane is not connected";
        return false;
    }
    if (nds_transfer_status_encode(&status, &wire, codec_error) != 0) {
        if (error != nullptr) *error = std::string("cannot encode transfer status: ") + codec_error;
        return false;
    }
    return write_full(fd_, &wire, sizeof(wire), error);
}

bool TcpControlPlane::receive_transfer_status(nds_transfer_status &status, std::string *error) const
{
    nds_transfer_status_wire_v1 wire{};
    char codec_error[NDS_WIRE_ERROR_CAPACITY]{};
    if (fd_ < 0) {
        if (error != nullptr) *error = "control plane is not connected";
        return false;
    }
    if (!read_full(fd_, &wire, sizeof(wire), error)) return false;
    if (nds_transfer_status_decode(&wire, &status, codec_error) != 0) {
        if (error != nullptr) *error = std::string("cannot decode transfer status: ") + codec_error;
        return false;
    }
    return true;
}

int TcpControlPlane::fd() const noexcept
{
    return fd_;
}

int TcpControlPlane::release() noexcept
{
    const int released = fd_;
    fd_ = -1;
    return released;
}

bool TcpControlPlane::read_full(int fd, void *buffer, std::size_t length, std::string *error)
{
    auto *cursor = static_cast<unsigned char *>(buffer);

    while (length != 0U) {
        const ssize_t result = read(fd, cursor, length);
        if (result == 0) {
            *error = "peer closed TCP control-plane connection";
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

bool TcpControlPlane::write_full(int fd, const void *buffer, std::size_t length, std::string *error)
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

ControlPlaneResult TcpControlPlane::exchange(int fd, const nds_rc_endpoint &local, bool client_order)
{
    nds_rc_endpoint_wire_v1 local_wire{};
    nds_rc_endpoint_wire_v1 peer_wire{};
    ControlPlaneResult result;
    char codec_error[NDS_WIRE_ERROR_CAPACITY]{};

    if (fd < 0) {
        result.error = "TCP control-plane socket is not open";
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

ControlPlaneResult TcpControlPlane::exchange_as_client(const nds_rc_endpoint &local) const
{
    return exchange(fd_, local, true);
}

ControlPlaneResult TcpControlPlane::exchange_as_server(const nds_rc_endpoint &local) const
{
    return exchange(fd_, local, false);
}

bool TcpControlPlane::connect(const std::string &ipv4, std::uint16_t port, std::uint32_t timeout_ms,
                               TcpControlPlane &connection, std::string *error)
{
    sockaddr_in address{};
    int socket_fd;
    int flags;
    int connect_result;

    if (connection.fd_ >= 0) {
        if (error != nullptr) {
            *error = "output control-plane object already owns a socket";
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

ControlPlaneResult TcpControlPlane::connect_and_exchange(const std::string &ipv4, std::uint16_t port,
                                                         const nds_rc_endpoint &local,
                                                         std::uint32_t timeout_ms)
{
    TcpControlPlane connection;
    std::string error;

    if (!connect(ipv4, port, timeout_ms, connection, &error)) {
        ControlPlaneResult result;
        result.error = std::move(error);
        return result;
    }
    return connection.exchange_as_client(local);
}

} // namespace nds

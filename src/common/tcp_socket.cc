#include "tcp_socket.hh"

#include <arpa/inet.h>
#include <cerrno>
#include <charconv>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>

namespace nds {
namespace {

std::string system_error(const char *operation) {
    return std::string(operation) + ": " + std::strerror(errno);
}

Result<void> wait_for_fd(int fd, short events, std::uint32_t timeout_ms) {
    pollfd descriptor{};
    descriptor.fd = fd;
    descriptor.events = events;
    const int result = poll(&descriptor, 1, static_cast<int>(timeout_ms));

    if (result == 0)
        return Error{ErrorCode::kTransport, "TCP exchange timeout"};
    if (result < 0)
        return Error{ErrorCode::kTransport, system_error("poll")};
    if ((descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0)
        return Error{ErrorCode::kTransport, "TCP exchange socket became unavailable"};
    return {};
}

}  // namespace

Result<TcpAddress> parse_tcp_address(const std::string &address) {
    const std::size_t separator = address.rfind(':');
    if (separator == std::string::npos || separator == 0U || separator + 1U == address.size())
        return Error{ErrorCode::kInvalidArgument, "TCP address must be IPv4:port"};
    TcpAddress parsed{address.substr(0U, separator), 0U};
    const std::string port_text = address.substr(separator + 1U);
    const auto [last, error] = std::from_chars(port_text.data(), port_text.data() + port_text.size(), parsed.port);
    in_addr ipv4{};
    if (inet_pton(AF_INET, parsed.ipv4.c_str(), &ipv4) != 1 || error != std::errc{} ||
        last != port_text.data() + port_text.size() || parsed.port == 0U) {
        return Error{ErrorCode::kInvalidArgument, "TCP address must be IPv4:port"};
    }
    return parsed;
}

TcpConnection::TcpConnection(int fd) noexcept : fd_(fd) {}

TcpConnection::~TcpConnection() {
    if (fd_ >= 0)
        (void)close(fd_);
}

TcpConnection::TcpConnection(TcpConnection &&other) noexcept : fd_(std::exchange(other.fd_, -1)) {}

TcpConnection &TcpConnection::operator=(TcpConnection &&other) noexcept {
    if (this != &other) {
        if (fd_ >= 0)
            (void)close(fd_);
        fd_ = std::exchange(other.fd_, -1);
    }
    return *this;
}

bool TcpConnection::is_open() const noexcept {
    return fd_ >= 0;
}

Result<void> TcpConnection::send(std::span<const std::byte> bytes) const {
    if (fd_ < 0)
        return Error{ErrorCode::kInvalidArgument, "an open TCP connection is required"};
    return write_full(fd_, bytes.data(), bytes.size());
}

Result<void> TcpConnection::receive(std::span<std::byte> bytes) const {
    if (fd_ < 0)
        return Error{ErrorCode::kInvalidArgument, "an open TCP connection is required"};
    return read_full(fd_, bytes.data(), bytes.size());
}

Result<void> TcpConnection::read_full(int fd, void *buffer, std::size_t length) {
    auto *cursor = static_cast<unsigned char *>(buffer);
    while (length != 0U) {
        const ssize_t result = read(fd, cursor, length);
        if (result == 0)
            return Error{ErrorCode::kTransport, "peer closed exchange channel"};
        if (result < 0) {
            if (errno == EINTR)
                continue;
            return Error{ErrorCode::kTransport, system_error("read")};
        }
        cursor += static_cast<std::size_t>(result);
        length -= static_cast<std::size_t>(result);
    }
    return {};
}

Result<void> TcpConnection::write_full(int fd, const void *buffer, std::size_t length) {
    const auto *cursor = static_cast<const unsigned char *>(buffer);
    while (length != 0U) {
        const ssize_t result = write(fd, cursor, length);
        if (result < 0) {
            if (errno == EINTR)
                continue;
            return Error{ErrorCode::kTransport, system_error("write")};
        }
        cursor += static_cast<std::size_t>(result);
        length -= static_cast<std::size_t>(result);
    }
    return {};
}

Result<TcpConnection> TcpConnection::connect(const std::string &ipv4, std::uint16_t port, std::uint32_t timeout_ms) {
    if (timeout_ms > static_cast<std::uint32_t>(std::numeric_limits<int>::max()))
        return Error{ErrorCode::kInvalidArgument, "TCP connection timeout is outside the supported range"};
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (inet_pton(AF_INET, ipv4.c_str(), &address.sin_addr) != 1)
        return Error{ErrorCode::kInvalidArgument, "invalid TCP peer IPv4 address: " + ipv4};

    const int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0)
        return Error{ErrorCode::kTransport, system_error("socket")};
    const int flags = fcntl(socket_fd, F_GETFL, 0);
    if (flags < 0 || fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK) != 0) {
        (void)close(socket_fd);
        return Error{ErrorCode::kTransport, system_error("fcntl")};
    }
    const int connect_result = ::connect(socket_fd, reinterpret_cast<const sockaddr *>(&address), sizeof(address));
    if (connect_result != 0 && errno != EINPROGRESS) {
        (void)close(socket_fd);
        return Error{ErrorCode::kTransport, system_error("connect")};
    }
    if (connect_result != 0) {
        const auto waited = wait_for_fd(socket_fd, POLLOUT, timeout_ms);
        if (!waited.ok()) {
            (void)close(socket_fd);
            return Error{waited.error()};
        }
        int socket_error = 0;
        socklen_t socket_error_length = sizeof(socket_error);
        if (getsockopt(socket_fd, SOL_SOCKET, SO_ERROR, &socket_error, &socket_error_length) != 0 ||
            socket_error != 0) {
            (void)close(socket_fd);
            return Error{ErrorCode::kTransport, socket_error != 0
                                                    ? std::string("connect: ") + std::strerror(socket_error)
                                                    : system_error("getsockopt")};
        }
    }
    if (fcntl(socket_fd, F_SETFL, flags) != 0) {
        (void)close(socket_fd);
        return Error{ErrorCode::kTransport, system_error("fcntl")};
    }
    return TcpConnection(socket_fd);
}

TcpListener::TcpListener(int fd) noexcept : fd_(fd) {}

TcpListener::~TcpListener() {
    if (fd_ >= 0)
        (void)close(fd_);
}

TcpListener::TcpListener(TcpListener &&other) noexcept : fd_(std::exchange(other.fd_, -1)) {}

TcpListener &TcpListener::operator=(TcpListener &&other) noexcept {
    if (this != &other) {
        if (fd_ >= 0)
            (void)close(fd_);
        fd_ = std::exchange(other.fd_, -1);
    }
    return *this;
}

Result<TcpListener> TcpListener::listen(const std::string &ipv4, std::uint16_t port, int backlog) {
    if (backlog <= 0)
        return Error{ErrorCode::kInvalidArgument, "TCP listener backlog must be positive"};
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (inet_pton(AF_INET, ipv4.c_str(), &address.sin_addr) != 1)
        return Error{ErrorCode::kInvalidArgument, "invalid TCP listen IPv4 address: " + ipv4};

    const int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0)
        return Error{ErrorCode::kTransport, system_error("socket")};
    int enabled = 1;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled)) != 0 ||
        bind(socket_fd, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0 ||
        ::listen(socket_fd, backlog) != 0) {
        const Error error{ErrorCode::kTransport, system_error("TCP listener setup")};
        (void)close(socket_fd);
        return Error{error};
    }
    return TcpListener(socket_fd);
}

bool TcpListener::is_open() const noexcept {
    return fd_ >= 0;
}

Result<TcpConnection> TcpListener::accept() const {
    if (fd_ < 0)
        return Error{ErrorCode::kInvalidArgument, "an open TCP listener is required"};
    int peer_fd;
    do {
        peer_fd = ::accept(fd_, nullptr, nullptr);
    } while (peer_fd < 0 && errno == EINTR);
    if (peer_fd < 0)
        return Error{ErrorCode::kTransport, system_error("accept")};
    return TcpConnection(peer_fd);
}

}  // namespace nds

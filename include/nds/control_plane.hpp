#ifndef NDS_CONTROL_PLANE_HPP
#define NDS_CONTROL_PLANE_HPP

#include "nds/rdma_wire_codec.h"

#include <cstdint>
#include <string>

namespace nds {

struct ControlPlaneResult {
    bool ok{false};
    nds_rc_endpoint peer{};
    std::string error;
};

/*
 * Project-owned TCP control plane for endpoint negotiation.  It transfers
 * exactly one fixed-size NDS endpoint record in each direction and deliberately
 * contains no HCCP, HCOMM, ACL, or verbs dependency.
 */
class TcpControlPlane {
public:
    explicit TcpControlPlane(int fd = -1) noexcept;
    ~TcpControlPlane();
    TcpControlPlane(const TcpControlPlane &) = delete;
    TcpControlPlane &operator=(const TcpControlPlane &) = delete;
    TcpControlPlane(TcpControlPlane &&other) noexcept;
    TcpControlPlane &operator=(TcpControlPlane &&other) noexcept;

    /* Open a TCP connection without exchanging. The returned object owns the socket. */
    static bool connect(const std::string &ipv4, std::uint16_t port, std::uint32_t timeout_ms,
                        TcpControlPlane &connection, std::string *error);

    static ControlPlaneResult connect_and_exchange(const std::string &ipv4, std::uint16_t port,
                                                   const nds_rc_endpoint &local,
                                                   std::uint32_t timeout_ms);

    /* Client ordering: send the local endpoint, then receive the peer endpoint. */
    ControlPlaneResult exchange_as_client(const nds_rc_endpoint &local) const;

    /* Server ordering: receive the peer endpoint, then send the local endpoint. */
    ControlPlaneResult exchange_as_server(const nds_rc_endpoint &local) const;

    /* These fixed-size records are exchanged after endpoint negotiation. */
    bool send_memory_descriptor(const nds_memory_descriptor &descriptor, std::string *error) const;
    bool receive_memory_descriptor(nds_memory_descriptor &descriptor, std::string *error) const;
    bool send_transfer_status(const nds_transfer_status &status, std::string *error) const;
    bool receive_transfer_status(nds_transfer_status &status, std::string *error) const;

    int fd() const noexcept;
    int release() noexcept;

private:
    static bool read_full(int fd, void *buffer, std::size_t length, std::string *error);
    static bool write_full(int fd, const void *buffer, std::size_t length, std::string *error);
    static ControlPlaneResult exchange(int fd, const nds_rc_endpoint &local, bool client_order);

    int fd_;
};

} // namespace nds

#endif

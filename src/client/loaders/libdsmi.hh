#ifndef NDS_CLIENT_LOADERS_LIBDSMI_HH
#define NDS_CLIENT_LOADERS_LIBDSMI_HH

#include "result.hh"
#include "shared_library.hh"

#include <cstdint>
#include <string_view>

class Libdsmi {
public:
    enum { IP_ADDRESS_V4 = 0 };

    struct IpAddress {
        union {
            std::uint8_t ip6[16];
            std::uint8_t ip4[4];
        } address;
        std::int32_t type;
    };

    using GetDeviceIpAddressFn = int (*)(int, int, int, IpAddress *, IpAddress *);

    Libdsmi() = default;
    ~Libdsmi() = default;
    Libdsmi(const Libdsmi &) = delete;
    Libdsmi &operator=(const Libdsmi &) = delete;
    Libdsmi(Libdsmi &&) noexcept = default;
    Libdsmi &operator=(Libdsmi &&) noexcept = default;

    static nds::Result<Libdsmi> open(std::string_view library_path = "libdrvdsmi_host.so");

    GetDeviceIpAddressFn get_device_ip_address{};

private:
    nds::client::SharedLibrary library_;
};

#endif

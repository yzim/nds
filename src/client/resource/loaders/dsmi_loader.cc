#include "dsmi_loader.hh"

#include "shared_library.hh"

#include <arpa/inet.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace {

enum class DsmiIpAddressType : std::int32_t {
    kV4 = 0,
};

struct DsmiIpAddress {
    union {
        std::uint8_t ip6[16];
        std::uint8_t ip4[4];
    } address;
    DsmiIpAddressType type;
};

static_assert(sizeof(DsmiIpAddress) == 20, "unexpected DSMI ip_addr_t ABI layout");
static_assert(offsetof(DsmiIpAddress, type) == 16, "unexpected DSMI ip_addr_t field offset");

using DsmiGetDeviceIpAddress = int (*)(int, int, int, DsmiIpAddress *, DsmiIpAddress *);

}  // namespace

namespace nds::client {

Result<std::string> dsmi_ipv4(std::uint32_t device_id) {
    constexpr int kDsmiRocePort = 1;
    constexpr int kPortId = 0;
    constexpr const char *kLibrary = "libdrvdsmi_host.so";

    auto library = SharedLibrary::open(kLibrary);
    if (!library)
        return unexpected(library.error());

    const auto get_ip = library->resolve_required<DsmiGetDeviceIpAddress>("dsmi_get_device_ip_address");
    if (!get_ip)
        return unexpected(get_ip.error());

    DsmiIpAddress address{};
    DsmiIpAddress mask{};
    address.type = DsmiIpAddressType::kV4;
    mask.type = DsmiIpAddressType::kV4;
    const int result = (*get_ip)(static_cast<int>(device_id), kDsmiRocePort, kPortId, &address, &mask);
    if (result != 0)
        return unexpected(ErrorCode::kRuntime, "dsmi_get_device_ip_address failed for device " +
                                                   std::to_string(device_id) + ": " + std::to_string(result));

    in_addr parsed{};
    std::memcpy(&parsed.s_addr, address.address.ip4, sizeof(parsed.s_addr));
    char text[INET_ADDRSTRLEN]{};
    if (inet_ntop(AF_INET, &parsed, text, sizeof(text)) == nullptr)
        return unexpected(ErrorCode::kRuntime,
                          "DSMI returned an invalid IPv4 address for device " + std::to_string(device_id));
    return std::string(text);
}

}  // namespace nds::client

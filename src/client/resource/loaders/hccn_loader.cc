#include "hccn_loader.hh"

#include <arpa/inet.h>
#include <dlfcn.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace {

// DSMI's public ABI (dsmi_common_interface.h). Keep this narrow transcription
// local so NDS does not require the target-only driver headers to build.
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

Result<std::string> hccn_ipv4(std::uint32_t device_id) {
    constexpr int kDsmiRocePort = 1;  // DSMI_ROCE_PORT
    constexpr int kPortId = 0;       // reserved by DSMI for the device RoCE port
    constexpr const char *kLibrary = "libdrvdsmi_host.so";
    constexpr const char *kFallbackLibrary = "/usr/local/Ascend/driver/lib64/driver/libdrvdsmi_host.so";

    void *library = dlopen(kLibrary, RTLD_NOW | RTLD_LOCAL);
    if (library == nullptr)
        library = dlopen(kFallbackLibrary, RTLD_NOW | RTLD_LOCAL);
    if (library == nullptr) {
        const char *error = dlerror();
        return unexpected(ErrorCode::kRuntime, "unable to load DSMI driver library: " +
                                                     std::string(error == nullptr ? "unknown error" : error));
    }

    (void)dlerror();
    void *symbol = dlsym(library, "dsmi_get_device_ip_address");
    const char *loader_error = dlerror();
    if (loader_error != nullptr || symbol == nullptr) {
        const std::string message = loader_error == nullptr ? "returned null" : loader_error;
        (void)dlclose(library);
        return unexpected(ErrorCode::kRuntime, "required DSMI symbol dsmi_get_device_ip_address is unavailable: " +
                                                     message);
    }
    DsmiGetDeviceIpAddress get_ip{};
    static_assert(sizeof(get_ip) == sizeof(symbol), "unexpected function pointer ABI");
    std::memcpy(&get_ip, &symbol, sizeof(get_ip));

    DsmiIpAddress address{};
    DsmiIpAddress mask{};
    address.type = DsmiIpAddressType::kV4;
    mask.type = DsmiIpAddressType::kV4;
    const int result = get_ip(static_cast<int>(device_id), kDsmiRocePort, kPortId, &address, &mask);
    (void)dlclose(library);
    if (result != 0)
        return unexpected(ErrorCode::kRuntime, "dsmi_get_device_ip_address failed for device " +
                                                     std::to_string(device_id) + ": " + std::to_string(result));

    in_addr parsed{};
    std::memcpy(&parsed.s_addr, address.address.ip4, sizeof(parsed.s_addr));
    char text[INET_ADDRSTRLEN]{};
    if (inet_ntop(AF_INET, &parsed, text, sizeof(text)) == nullptr)
        return unexpected(ErrorCode::kRuntime, "DSMI returned an invalid IPv4 address for device " +
                                                     std::to_string(device_id));
    return std::string(text);
}

}  // namespace nds::client

#include "libdsmi.hh"

#include <cstddef>

static_assert(sizeof(Libdsmi::IpAddress) == 20, "unexpected DSMI ip_addr_t ABI layout");
static_assert(offsetof(Libdsmi::IpAddress, type) == 16, "unexpected DSMI ip_addr_t field offset");

nds::Result<Libdsmi> Libdsmi::open(std::string_view library_path) {
    NDS_ASSIGN_OR_RETURN(nds::client::SharedLibrary library, nds::client::SharedLibrary::open(library_path));
    Libdsmi libdsmi;
    NDS_RETURN_IF_ERROR(library.resolve_required("dsmi_get_device_ip_address", &libdsmi.get_device_ip_address));
    libdsmi.library_ = std::move(library);
    return libdsmi;
}

#ifndef NDS_CLIENT_LOADERS_DSMI_LOADER_HH
#define NDS_CLIENT_LOADERS_DSMI_LOADER_HH

#include "nds/result.hh"

#include <cstdint>
#include <string>

namespace nds::client {

Result<std::string> dsmi_ipv4(std::uint32_t device_id);

}  // namespace nds::client

#endif

#ifndef NDS_HCCN_LOADER_HH
#define NDS_HCCN_LOADER_HH

#include "nds/result.hh"

#include <cstdint>
#include <string>

namespace nds::client {

Result<std::string> hccn_ipv4(std::uint32_t device_id);

}  // namespace nds::client

#endif

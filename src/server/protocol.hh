#ifndef NDS_SERVER_PROTOCOL_HH
#define NDS_SERVER_PROTOCOL_HH

#include "transport.hh"

#include <cstdint>
#include <string>
#include <vector>

namespace nds::server {
Result<void> serve_request(Connection *connection, std::vector<unsigned char> *storage, std::uint32_t timeout_ms);
}
#endif

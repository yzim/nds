#ifndef NDS_CPU_PROTOCOL_HH
#define NDS_CPU_PROTOCOL_HH

#include "transport.hh"

#include <cstdint>
#include <string>
#include <vector>

namespace nds::cpu {
bool serve_storage_request(Connection *connection, std::vector<unsigned char> *storage, std::uint32_t timeout_ms,
                           std::string *error);
}
#endif

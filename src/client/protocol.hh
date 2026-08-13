#ifndef NDS_CLIENT_PROTOCOL_HH
#define NDS_CLIENT_PROTOCOL_HH

#include "transport.hh"

#include <cstdint>
#include <string>

namespace nds::client {

struct Request {
    std::uint64_t request_id{};
    std::uint16_t operation{};
    std::uint64_t offset{};
    std::uint32_t length{};
    DeviceBuffer *data{};
};

bool execute_request(Connection *connection, const Request &request, std::string *error);

}  // namespace nds::client

#endif

#ifndef NDS_NPU_STORAGE_PROTOCOL_HH
#define NDS_NPU_STORAGE_PROTOCOL_HH

#include "transport/connection.hh"

#include <cstdint>
#include <string>

namespace nds::npu {

struct StorageRequest {
    std::uint64_t request_id{};
    std::uint16_t operation{};
    std::uint64_t offset{};
    std::uint32_t length{};
    DeviceBuffer *data{};
};

bool execute_storage_request(Connection *connection, const StorageRequest &request, std::string *error);

}  // namespace nds::npu

#endif

#ifndef NDS_CLIENT_STORAGE_HH
#define NDS_CLIENT_STORAGE_HH

#include "connection.hh"

#include <cstddef>
#include <cstdint>

namespace nds::client {

/* Host implementation of the NDS storage API. Device implementations use the same semantics. */
class StorageClient {
public:
    Result<void> open(const ConnectionConfig &config);
    Result<void> allocate(std::size_t size, DeviceBuffer *buffer);
    Result<void> copy_to_device(DeviceBuffer *buffer, const void *source, std::size_t size);
    Result<void> copy_from_device(void *destination, const DeviceBuffer &buffer, std::size_t size);
    Result<void> read(std::uint64_t offset, DeviceBuffer *data, std::uint32_t length);
    Result<void> write(std::uint64_t offset, DeviceBuffer *data, std::uint32_t length);

    std::uint64_t capacity() const noexcept;

private:
    Result<void> execute(std::uint16_t operation, std::uint64_t offset, DeviceBuffer *data, std::uint32_t length);
    Result<std::uint64_t> exchange_bootstrap();
    Connection connection_;
    DeviceBuffer command_buffer_;
    DeviceBuffer completion_buffer_;
    RegisteredRegion command_region_;
    RegisteredRegion completion_region_;
    std::uint64_t capacity_{};
    std::uint64_t next_request_id_{};
    bool opened_{};
    bool request_submitted_{};
};

}  // namespace nds::client

#endif

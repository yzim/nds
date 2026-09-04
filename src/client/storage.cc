#include "storage.hh"

#include <chrono>
#include <cstddef>
#include <cstring>
#include <limits>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace nds::client {
namespace {

constexpr std::uint32_t kStorageSlotsPerQp = 16U;
constexpr std::uint32_t kStorageSlotIndexBits = 16U;
constexpr std::uint32_t kStorageSlotIndexLimit = 1U << kStorageSlotIndexBits;

bool valid_timeout(std::uint32_t timeout_ms) {
    return timeout_ms != 0U && timeout_ms <= static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max());
}

}  // namespace

StorageClient::~StorageClient() = default;

Result<void> StorageClient::open(Runtime *runtime, Transport *transport) {
    if (opened_ || runtime == nullptr || transport == nullptr || !runtime->initialized())
        return Error{ErrorCode::kInvalidArgument, "storage client requires one open runtime and transport"};
    if (transport->qp_count() == 0U || transport->qp_count() > UINT16_MAX)
        return Error{ErrorCode::kInvalidArgument, "storage client requires a supported transport QP count"};
    if (transport->qp_count() > std::numeric_limits<std::size_t>::max() / kStorageSlotsPerQp)
        return Error{ErrorCode::kInvalidArgument, "storage slot count overflows"};

    runtime_ = runtime;
    transport_ = transport;
    slots_.resize(transport_->qp_count() * kStorageSlotsPerQp);
    allocated_slots_.assign(slots_.size(), false);
    if (slots_.size() == 0U || slots_.size() >= kStorageSlotIndexLimit)
        return Error{ErrorCode::kInvalidArgument, "storage slot count is outside the packed slot-id range"};
    if (slots_.size() > std::numeric_limits<std::size_t>::max() / kStorageCommandBytes ||
        slots_.size() > std::numeric_limits<std::size_t>::max() / kStorageCompletionBytes)
        return Error{ErrorCode::kInvalidArgument, "storage slot allocation size overflows"};

    const auto register_region = [this](const MemoryBuffer &buffer, MemoryAccess access,
                                        const char *name) -> Result<MemoryRegion> {
        auto registered = transport_->register_memory(buffer, access);
        if (!registered.ok())
            return Error{registered.error().code,
                         std::string(name) + " registration failed: " + registered.error().message};
        return std::move(registered).value();
    };

    NDS_ASSIGN_OR_RETURN(bootstrap_buffer_, runtime_->allocate(kStorageBootstrapBytes, MemoryLocation::Device));
    NDS_ASSIGN_OR_RETURN(bootstrap_region_,
                         register_region(bootstrap_buffer_, MemoryAccess::LocalWrite | MemoryAccess::RemoteRead,
                                         "storage bootstrap buffer"));
    NDS_ASSIGN_OR_RETURN(command_buffer_,
                         runtime_->allocate(slots_.size() * kStorageCommandBytes, MemoryLocation::Device));
    NDS_ASSIGN_OR_RETURN(command_region_, register_region(command_buffer_,
                                                          MemoryAccess::LocalWrite | MemoryAccess::RemoteWrite |
                                                              MemoryAccess::RemoteRead,
                                                          "storage command buffer"));
    NDS_ASSIGN_OR_RETURN(completion_buffer_,
                         runtime_->allocate(slots_.size() * kStorageCompletionBytes, MemoryLocation::Device));
    NDS_ASSIGN_OR_RETURN(completion_region_, register_region(completion_buffer_,
                                                             MemoryAccess::LocalWrite | MemoryAccess::RemoteWrite |
                                                                 MemoryAccess::RemoteRead,
                                                             "storage completion buffer"));
    for (std::size_t index = 0U; index < slots_.size(); ++index) {
        slots_[index].command_offset = index * kStorageCommandBytes;
        slots_[index].completion_offset = index * kStorageCompletionBytes;
        slots_[index].qp_index = static_cast<std::uint32_t>(index % transport_->qp_count());
    }

    NDS_ASSIGN_OR_RETURN(namespace_buffer_, runtime_->allocate(kStorageNamespaceBytes, MemoryLocation::Device));
    NDS_ASSIGN_OR_RETURN(namespace_region_, register_region(namespace_buffer_,
                                                            MemoryAccess::LocalWrite | MemoryAccess::RemoteWrite |
                                                                MemoryAccess::RemoteRead,
                                                            "storage namespace buffer"));

    const MemoryLocation descriptor_location =
        transport_->backend().mode == BackendMode::Ra ? MemoryLocation::Host : MemoryLocation::Device;
    const std::size_t descriptor_bytes = slots_.size() * sizeof(NdsStorageSlotDescriptor);
    NDS_ASSIGN_OR_RETURN(slot_descriptors_buffer_, runtime_->allocate(descriptor_bytes, descriptor_location));
    std::vector<NdsStorageSlotDescriptor> descriptors(slots_.size());
    for (std::size_t index = 0U; index < slots_.size(); ++index) {
        descriptors[index] = NdsStorageSlotDescriptor{
            .command_buffer = {.address = command_region_.address() + slots_[index].command_offset,
                               .length = kStorageCommandBytes,
                               .local_key = command_region_.local_key()},
            .completion_buffer = {.address = completion_region_.address() + slots_[index].completion_offset,
                                  .length = kStorageCompletionBytes,
                                  .local_key = completion_region_.local_key()},
            .qp_index = slots_[index].qp_index,
            .reserved = 0U,
        };
    }
    if (const auto copied = runtime_->copy_to(&slot_descriptors_buffer_, descriptors.data(), descriptor_bytes);
        !copied.ok())
        return Error{copied.error().code, "storage slot-descriptor copy failed: " + copied.error().message};

    const MemoryLocation state_location =
        transport_->backend().mode == BackendMode::Ra ? MemoryLocation::Host : MemoryLocation::Device;
    NDS_ASSIGN_OR_RETURN(storage_states_buffer_,
                         runtime_->allocate(slots_.size() * sizeof(NdsStorageState), state_location));
    std::vector<NdsStorageState> states(slots_.size());
    if (const auto copied =
            runtime_->copy_to(&storage_states_buffer_, states.data(), states.size() * sizeof(NdsStorageState));
        !copied.ok())
        return Error{copied.error().code, "storage state initialization failed: " + copied.error().message};

    const std::size_t slot_table_bytes = slots_.size() * sizeof(StorageSlot);
    const MemoryLocation slot_table_location =
        transport_->backend().mode == BackendMode::Ra ? MemoryLocation::HostPinned : MemoryLocation::Device;
    NDS_ASSIGN_OR_RETURN(slot_table_buffer_, runtime_->allocate(slot_table_bytes, slot_table_location));
    std::vector<StorageSlot> slot_table(slots_.size());
    for (std::size_t index = 0U; index < slots_.size(); ++index) {
        slot_table[index] = StorageSlot{
            .command = {command_region_.address() + slots_[index].command_offset, kStorageCommandBytes,
                        command_region_.remote_key()},
            .completion = {completion_region_.address() + slots_[index].completion_offset, kStorageCompletionBytes,
                           completion_region_.remote_key()},
            .qp_index = slots_[index].qp_index,
            .reserved = 0U,
        };
    }
    if (const auto copied = runtime_->copy_to(&slot_table_buffer_, slot_table.data(), slot_table_bytes); !copied.ok())
        return Error{copied.error().code, "storage slot-table copy failed: " + copied.error().message};
    NDS_ASSIGN_OR_RETURN(
        slot_table_region_,
        register_region(slot_table_buffer_, MemoryAccess::LocalWrite | MemoryAccess::RemoteRead, "storage slot table"));

    storage_descriptor_ = NdsStorageDescriptor{
        .transport = transport_->device_transport(),
        .slot_descriptors_address = reinterpret_cast<std::uint64_t>(slot_descriptors_buffer_.data()),
        .storage_states_address = reinterpret_cast<std::uint64_t>(storage_states_buffer_.data()),
        .slot_count = static_cast<std::uint32_t>(slots_.size()),
        .reserved = 0U,
        .capacity = 0U,
    };
    bootstrap_descriptor_ = NdsStorageBootstrapDescriptor{
        .transport = transport_->device_transport(),
        .bootstrap = {.address = bootstrap_region_.address(),
                      .length = kStorageBootstrapBytes,
                      .local_key = bootstrap_region_.local_key()},
    };
    const nds::QpInfo &local = transport_->local_qp_info();
    next_command_id_ = (static_cast<std::uint64_t>(local.qp_num) << 32U) | local.psn;
    if (next_command_id_ == 0U)
        next_command_id_ = 1U;
    opened_ = true;
    NDS_RETURN_IF_ERROR(prepare_bootstrap());
    return {};
}

Result<MemoryRegion> StorageClient::register_memory(const MemoryBuffer &buffer, MemoryAccess access) {
    if (!opened_ || transport_ == nullptr)
        return Error{ErrorCode::kInvalidArgument, "storage memory registration requires an open client"};
    return transport_->register_memory(buffer, access);
}

std::uint64_t StorageClient::capacity() const noexcept {
    return capacity_;
}

std::size_t StorageClient::slot_count() const noexcept {
    return slots_.size();
}

const NdsStorageDescriptor &StorageClient::descriptor() const noexcept {
    return storage_descriptor_;
}

const NdsStorageBootstrapDescriptor &StorageClient::bootstrap_descriptor() const noexcept {
    return bootstrap_descriptor_;
}

Result<std::uint32_t> StorageClient::allocate_slot() {
    if (!opened_ || transport_ == nullptr)
        return Error{ErrorCode::kInvalidArgument, "storage client is not open"};
    for (std::size_t attempt = 0U; attempt < slots_.size(); ++attempt) {
        const std::size_t index = (next_slot_ + attempt) % slots_.size();
        if (!allocated_slots_[index]) {
            allocated_slots_[index] = true;
            next_slot_ = (index + 1U) % slots_.size();
            const std::uint32_t slot_id = nds_storage_slot_id(slots_[index].qp_index, index);
            const NdsStorageState state{
                .command_id = allocate_command_id(), .expected_bytes = 0U, .in_flight = 0U, .status = 0};
            if (const auto state_result = clear_state(static_cast<std::uint32_t>(index)); !state_result.ok()) {
                allocated_slots_[index] = false;
                return state_result.error();
            }
            if (storage_states_buffer_.location() == MemoryLocation::Host) {
                std::memcpy(static_cast<std::byte *>(storage_states_buffer_.data()) + index * sizeof(NdsStorageState),
                            &state, sizeof(state));
            } else if (const auto copied = runtime_->copy_host_to_device(
                           static_cast<std::byte *>(storage_states_buffer_.data()) + index * sizeof(NdsStorageState),
                           &state, sizeof(state));
                       !copied.ok()) {
                allocated_slots_[index] = false;
                return copied.error();
            }
            return slot_id;
        }
    }
    return Error{ErrorCode::kTransport, "all storage slots have a command in flight"};
}

Result<std::uint32_t> StorageClient::allocate_slot(std::uint32_t queue_index) {
    if (!opened_ || transport_ == nullptr || queue_index >= transport_->qp_count())
        return Error{ErrorCode::kInvalidArgument, "storage queue index is out of range"};
    if (slots_.empty())
        return Error{ErrorCode::kTransport, "storage has no slots"};
    for (std::size_t attempt = 0U; attempt < slots_.size(); ++attempt) {
        const std::size_t index = (next_slot_ + attempt) % slots_.size();
        if (!allocated_slots_[index] && slots_[index].qp_index == queue_index) {
            allocated_slots_[index] = true;
            next_slot_ = (index + 1U) % slots_.size();
            const NdsStorageState state{
                .command_id = allocate_command_id(), .expected_bytes = 0U, .in_flight = 0U, .status = 0};
            if (storage_states_buffer_.location() == MemoryLocation::Host) {
                std::memcpy(static_cast<std::byte *>(storage_states_buffer_.data()) + index * sizeof(NdsStorageState),
                            &state, sizeof(state));
            } else if (const auto copied = runtime_->copy_host_to_device(
                           static_cast<std::byte *>(storage_states_buffer_.data()) + index * sizeof(NdsStorageState),
                           &state, sizeof(state));
                       !copied.ok()) {
                allocated_slots_[index] = false;
                return copied.error();
            }
            return nds_storage_slot_id(queue_index, static_cast<std::uint32_t>(index));
        }
    }
    return Error{ErrorCode::kTransport, "all storage slots on the requested queue are in flight"};
}

Result<void> StorageClient::release_slot(std::uint32_t slot_id) {
    if (!opened_ || transport_ == nullptr)
        return Error{ErrorCode::kInvalidArgument, "storage slot handle is invalid"};
    const std::uint32_t slot_index = nds_storage_slot_id_index(slot_id);
    const std::uint32_t queue_index = nds_storage_slot_id_queue(slot_id);
    if (slot_index >= slots_.size() || slots_[slot_index].qp_index != queue_index)
        return Error{ErrorCode::kInvalidArgument, "storage slot handle is invalid"};
    if (!allocated_slots_[slot_index])
        return Error{ErrorCode::kInvalidArgument, "storage slot is not allocated"};
    NDS_RETURN_IF_ERROR(clear_state(slot_index));
    allocated_slots_[slot_index] = false;
    return {};
}

Result<void> StorageClient::clear_state(std::uint32_t slot_index) {
    if (slot_index >= slots_.size())
        return Error{ErrorCode::kInvalidArgument, "storage state slot is out of range"};
    const NdsStorageState empty{};
    auto *address = static_cast<std::byte *>(storage_states_buffer_.data()) + slot_index * sizeof(NdsStorageState);
    if (storage_states_buffer_.location() == MemoryLocation::Host) {
        std::memcpy(address, &empty, sizeof(empty));
        return {};
    }
    if (runtime_ == nullptr)
        return Error{ErrorCode::kInvalidArgument, "device storage state requires a runtime"};
    return runtime_->copy_host_to_device(address, &empty, sizeof(empty));
}

std::uint64_t StorageClient::allocate_command_id() noexcept {
    const std::uint64_t command_id = next_command_id_++;
    if (next_command_id_ == 0U)
        ++next_command_id_;
    return command_id == 0U ? allocate_command_id() : command_id;
}

Result<void> StorageClient::prepare_bootstrap() {
    if (slots_.empty())
        return Error{ErrorCode::kInvalidArgument, "storage bootstrap requires a slot"};
    const StorageBootstrap bootstrap{
        .completion = {completion_region_.address(), kStorageCompletionBytes, completion_region_.remote_key()},
        .namespace_response = {namespace_region_.address(), namespace_region_.length(), namespace_region_.remote_key()},
        .slots = {slot_table_region_.address(), slot_table_region_.length(), slot_table_region_.remote_key()},
        .slot_count = static_cast<std::uint32_t>(slots_.size()),
    };
    std::uint8_t bootstrap_bytes[kStorageBootstrapBytes]{};
    std::uint8_t namespace_bytes[kStorageNamespaceBytes]{};
    if (serialize_storage_bootstrap(bootstrap, bootstrap_bytes, sizeof(bootstrap_bytes)) != StorageSerdeResult::Ok)
        return Error{ErrorCode::kProtocol, "invalid storage bootstrap record"};
    if (const auto copied = runtime_->copy_to(&namespace_buffer_, namespace_bytes, sizeof(namespace_bytes));
        !copied.ok())
        return Error{copied.error().code, "storage namespace initialization failed: " + copied.error().message};
    if (const auto copied = runtime_->copy_to(&bootstrap_buffer_, bootstrap_bytes, sizeof(bootstrap_bytes));
        !copied.ok())
        return Error{copied.error().code, "storage bootstrap initialization failed: " + copied.error().message};
    return transport_->ready();
}

Result<std::uint64_t> StorageClient::observe_namespace(std::uint32_t timeout_ms) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        std::uint8_t bytes[kStorageNamespaceBytes]{};
        if (const auto copied = runtime_->copy_from(bytes, namespace_buffer_, sizeof(bytes)); !copied.ok())
            return copied.error();
        StorageNamespace storage_namespace{};
        if (deserialize_storage_namespace(bytes, sizeof(bytes), &storage_namespace) == StorageSerdeResult::Ok)
            return storage_namespace.capacity;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return Error{ErrorCode::kProtocol, "timed out waiting for storage namespace response"};
}

Result<void> StorageClient::complete_bootstrap(std::uint32_t timeout_ms) {
    if (!opened_ || !valid_timeout(timeout_ms))
        return Error{ErrorCode::kInvalidArgument, "storage bootstrap completion requires an open client and timeout"};
    NDS_ASSIGN_OR_RETURN(const std::uint64_t capacity, observe_namespace(timeout_ms));
    capacity_ = capacity;
    storage_descriptor_.capacity = capacity_;
    return {};
}

}  // namespace nds::client

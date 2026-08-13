#ifndef NDS_STORAGE_SUBMISSION_HPP
#define NDS_STORAGE_SUBMISSION_HPP

#include "nds/npu_ra_qp.hh"

#include <cstdint>
#include <string>

namespace nds {

class NpuRaContext;

struct StorageSubmissionConfig {
    NpuRaSubmissionMode mode{NpuRaSubmissionMode::HostRa};
    std::uint32_t logical_device_id{};
    std::uint32_t service_level{};
    std::string aicpu_kernel_config;
    std::string aiv_kernel;
};

/* Posts exactly one NDS storage-command Send from registered NPU memory. */
bool submit_storage_command(NpuRaContext &context, NpuRaQp &qp,
                            const StorageSubmissionConfig &config,
                            std::uint64_t command_address, std::uint32_t command_length,
                            std::uint32_t command_lkey, std::string &error);

} // namespace nds

#endif

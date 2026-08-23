#include "api.h"
#include "internal.h"

#include <cstdint>

namespace {

constexpr uint32_t kMaxIdlePolls = 10000000U;

__aicore__ inline void store_result(__gm__ const NdsDeviceRdmaBenchmarkArgs *args,
                                    const NdsDeviceRdmaBenchmarkResult &result) {
    if (args != nullptr && args->result_address != 0U) {
        __gm__ NdsDeviceRdmaBenchmarkResult *destination =
            reinterpret_cast<__gm__ NdsDeviceRdmaBenchmarkResult *>(args->result_address);
        destination->status = result.status;
        destination->reserved = result.reserved;
        destination->operations_completed = result.operations_completed;
        destination->bytes_transferred = result.bytes_transferred;
        destination->wqe_count = result.wqe_count;
        destination->poll_count = result.poll_count;
        destination->completion_status = result.completion_status;
        destination->completion_vendor_error = result.completion_vendor_error;
        NdsAivCacheSync(reinterpret_cast<__gm__ uint8_t *>(args->result_address), sizeof(result));
    }
}

__aicore__ inline void fail(__gm__ const NdsDeviceRdmaBenchmarkArgs *args, uint32_t status,
                            const NdsDeviceRdmaBenchmarkResult &partial) {
    NdsDeviceRdmaBenchmarkResult result = partial;
    result.status = status;
    store_result(args, result);
}

__aicore__ inline bool valid_args(__gm__ const NdsDeviceRdmaBenchmarkArgs *args) {
    return args != nullptr && args->result_address != 0U && NdsAivValidQp(&args->transport.control_qp) &&
           args->local_address != 0U &&
           args->remote_address != 0U && args->local_key != 0U && args->remote_key != 0U && args->bytes != 0U &&
           args->iterations != 0U && args->in_flight != 0U && args->max_wrs_per_window != 0U &&
           (args->operation == NDS_DEVICE_BENCHMARK_READ || args->operation == NDS_DEVICE_BENCHMARK_WRITE);
}

}  // namespace

NDS_AIV_DEVICE_API_LINKAGE __aicore__ void NdsAivRdmaBenchmarkImpl(
    __gm__ NdsDeviceRdmaBenchmarkArgs *args, AscendC::TBuf<> *scratch) {
    NdsDeviceRdmaBenchmarkResult result{};
    if (!valid_args(args) || scratch == nullptr) {
        fail(args, NDS_DEVICE_BENCHMARK_INVALID_ARGUMENT, result);
        return;
    }

    const uint32_t chunk_bytes = args->max_wr_bytes == 0U || args->max_wr_bytes > args->bytes
                                     ? args->bytes
                                     : args->max_wr_bytes;
    const uint64_t chunks_per_request =
        (static_cast<uint64_t>(args->bytes) + chunk_bytes - 1U) / chunk_bytes;
    if (chunks_per_request > args->max_wrs_per_window) {
        fail(args, NDS_DEVICE_BENCHMARK_INVALID_ARGUMENT, result);
        return;
    }
    const uint32_t requests_per_window =
        args->max_wrs_per_window / static_cast<uint32_t>(chunks_per_request) < args->in_flight
            ? args->max_wrs_per_window / static_cast<uint32_t>(chunks_per_request)
            : args->in_flight;
    if (requests_per_window == 0U) {
        fail(args, NDS_DEVICE_BENCHMARK_INVALID_ARGUMENT, result);
        return;
    }
    const uint32_t doorbell_interval = args->post_mode & NDS_DEVICE_BENCHMARK_POST_MODE_MASK;
    const uint32_t signal_interval =
        (args->post_mode >> NDS_DEVICE_BENCHMARK_SIGNAL_EVERY_SHIFT) & NDS_DEVICE_BENCHMARK_POST_MODE_MASK;
    if (doorbell_interval == 0U) {
        fail(args, NDS_DEVICE_BENCHMARK_INVALID_ARGUMENT, result);
        return;
    }

    for (uint32_t completed = 0U; completed < args->iterations;) {
        const uint32_t request_count = args->iterations - completed < requests_per_window
                                           ? args->iterations - completed
                                           : requests_per_window;
        const uint64_t window_wrs = static_cast<uint64_t>(request_count) * chunks_per_request;
        uint64_t expected_completions = 0U;
        uint64_t window_wr_index = 0U;
        for (uint32_t request = 0U; request < request_count; ++request) {
            uint64_t request_offset{};
            if (args->request_offsets_address != 0U) {
                __gm__ uint64_t *offsets = reinterpret_cast<__gm__ uint64_t *>(args->request_offsets_address);
                const uint64_t offset_index = args->request_offset_start + completed + request;
                NdsAivCacheSync(reinterpret_cast<__gm__ uint8_t *>(&offsets[offset_index]), sizeof(request_offset));
                request_offset = offsets[offset_index];
            } else {
                const uint32_t slot = (completed + request) % args->in_flight;
                request_offset = static_cast<uint64_t>(slot) * args->bytes;
            }
            const uint64_t local_base = args->local_address + request_offset;
            const uint64_t remote_base = args->remote_address + request_offset;
            for (uint64_t chunk_offset = 0U; chunk_offset < args->bytes; chunk_offset += chunk_bytes) {
                const uint32_t length = chunk_offset + chunk_bytes > args->bytes
                                            ? args->bytes - static_cast<uint32_t>(chunk_offset)
                                            : chunk_bytes;
                const uint64_t wr_index = result.wqe_count;
                const bool final_wqe = window_wr_index + 1U == window_wrs;
                const bool doorbell_boundary = final_wqe || (window_wr_index + 1U) % doorbell_interval == 0U;
                const bool signal_boundary =
                    final_wqe || (signal_interval != 0U && (window_wr_index + 1U) % signal_interval == 0U);
                const NdsDeviceSendWr wr{
                    1U + wr_index,
                    args->operation == NDS_DEVICE_BENCHMARK_READ ? NDS_DEVICE_WR_RDMA_READ : NDS_DEVICE_WR_RDMA_WRITE,
                    (signal_boundary ? static_cast<uint32_t>(NDS_DEVICE_SEND_SIGNALED) : 0U) |
                        (doorbell_boundary ? 0U : static_cast<uint32_t>(NDS_DEVICE_SEND_DEFER_DOORBELL)),
                    {local_base + chunk_offset, length, args->local_key},
                    remote_base + chunk_offset,
                    args->remote_key,
                    0U};
                args->return_value = -1;
                NdsAivCacheSync(reinterpret_cast<__gm__ uint8_t *>(&args->return_value), sizeof(args->return_value));
                if (args->operation == NDS_DEVICE_BENCHMARK_READ)
                    NdsAivRdmaReadImpl(&args->transport, &wr, &args->return_value, scratch);
                else
                    NdsAivRdmaWriteImpl(&args->transport, &wr, &args->return_value, scratch);
                if (args->return_value != 0) {
                    fail(args, NDS_DEVICE_BENCHMARK_POST_FAILED, result);
                    return;
                }
                ++result.wqe_count;
                ++window_wr_index;
                if (signal_boundary)
                    ++expected_completions;
            }
        }

        uint64_t completed_wrs = 0U;
        uint32_t idle_polls = 0U;
        while (completed_wrs < expected_completions) {
            args->return_value = -1;
            NdsAivCacheSync(reinterpret_cast<__gm__ uint8_t *>(&args->return_value), sizeof(args->return_value));
            NdsAivPollCqImpl(&args->transport.control_qp, 1U, 1U,
                             reinterpret_cast<__gm__ NdsDeviceWc *>(args->result_address),
                             &args->return_value, scratch);
            ++result.poll_count;
            if (args->return_value < 0) {
                fail(args, NDS_DEVICE_BENCHMARK_POLL_FAILED, result);
                return;
            }
            if (args->return_value == 0) {
                if (++idle_polls >= kMaxIdlePolls) {
                    fail(args, NDS_DEVICE_BENCHMARK_POLL_TIMEOUT, result);
                    return;
                }
                continue;
            }
            idle_polls = 0U;
            for (int32_t index = 0; index < args->return_value; ++index) {
                __gm__ const NdsDeviceWc *completion =
                    &reinterpret_cast<__gm__ const NdsDeviceWc *>(args->result_address)[index];
                if (completion->status != 0U) {
                    result.completion_status = completion->status;
                    result.completion_vendor_error = completion->vendor_error;
                    fail(args, NDS_DEVICE_BENCHMARK_COMPLETION_FAILED, result);
                    return;
                }
                ++completed_wrs;
            }
        }
        result.operations_completed += request_count;
        result.bytes_transferred += static_cast<uint64_t>(request_count) * args->bytes;
        completed += request_count;
    }
    result.status = NDS_DEVICE_BENCHMARK_SUCCESS;
    store_result(args, result);
}

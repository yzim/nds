#include "api.h"
#include "internal.h"

#include <stdint.h>

namespace {

constexpr uint32_t kMaxIdlePolls = 10000000U;
constexpr uint32_t kMaxLinkedWrs = 64U;

void store_result(const NdsDeviceRdmaBenchmarkArgs *args, const NdsDeviceRdmaBenchmarkResult &result) {
    if (args != nullptr && args->result_address != 0U)
        *reinterpret_cast<NdsDeviceRdmaBenchmarkResult *>(args->result_address) = result;
    NdsAicpuBarrier();
}

uint32_t fail(const NdsDeviceRdmaBenchmarkArgs *args, uint32_t status,
              const NdsDeviceRdmaBenchmarkResult &partial) {
    NdsDeviceRdmaBenchmarkResult result = partial;
    result.status = status;
    store_result(args, result);
    return kNdsAicpuSuccess;
}

}  // namespace

extern "C" uint32_t NdsAicpuRdmaBenchmarkImpl(const NdsDeviceRdmaBenchmarkArgs *args) {
    NdsDeviceRdmaBenchmarkResult result{};
    const uint32_t post_mode = args == nullptr ? 0U : args->post_mode & NDS_DEVICE_BENCHMARK_POST_MODE_MASK;
    const uint32_t poll_batch = args == nullptr
                                     ? 1U
                                     : (args->post_mode >> NDS_DEVICE_BENCHMARK_POLL_BATCH_SHIFT) & 0xffU;
    const uint32_t signal_every = args == nullptr
                                      ? 0U
                                      : (args->post_mode >> NDS_DEVICE_BENCHMARK_SIGNAL_EVERY_SHIFT) & 0xffU;
    const uint32_t requested_linked_wrs = args == nullptr
                                               ? 0U
                                               : (args->post_mode >> NDS_DEVICE_BENCHMARK_LINKED_WRS_SHIFT) & 0xffU;
    const uint32_t linked_wrs = requested_linked_wrs == 0U ? 16U : requested_linked_wrs;
    if (args == nullptr || args->result_address == 0U || !NdsAicpuValidQp(&args->transport.control_qp) ||
        (args->transport.control_qp.qp_mode != NDS_DEVICE_QP_MODE_NORMAL &&
         args->transport.control_qp.qp_mode != NDS_DEVICE_QP_MODE_OPBASE_EXT) ||
        args->local_address == 0U ||
        args->remote_address == 0U || args->local_key == 0U || args->remote_key == 0U || args->bytes == 0U ||
        args->iterations == 0U || args->in_flight == 0U || args->max_wrs_per_window == 0U ||
        (args->operation != NDS_DEVICE_BENCHMARK_READ && args->operation != NDS_DEVICE_BENCHMARK_WRITE) ||
        post_mode > NDS_DEVICE_BENCHMARK_POST_LINKED || poll_batch == 0U ||
        poll_batch > NDS_DEVICE_MAX_COMPLETIONS || linked_wrs > kMaxLinkedWrs) {
        return fail(args, NDS_DEVICE_BENCHMARK_INVALID_ARGUMENT, result);
    }

    const uint32_t chunk_bytes = args->max_wr_bytes == 0U || args->max_wr_bytes > args->bytes
                                     ? args->bytes
                                     : args->max_wr_bytes;
    const uint64_t chunks_per_request =
        (static_cast<uint64_t>(args->bytes) + chunk_bytes - 1U) / chunk_bytes;
    if (chunks_per_request > args->max_wrs_per_window)
        return fail(args, NDS_DEVICE_BENCHMARK_INVALID_ARGUMENT, result);
    const uint32_t requests_per_window =
        args->max_wrs_per_window / static_cast<uint32_t>(chunks_per_request) < args->in_flight
            ? args->max_wrs_per_window / static_cast<uint32_t>(chunks_per_request)
            : args->in_flight;
    if (requests_per_window == 0U)
        return fail(args, NDS_DEVICE_BENCHMARK_INVALID_ARGUMENT, result);
    const auto *request_offsets = reinterpret_cast<const uint64_t *>(args->request_offsets_address);
    const auto *remote_request_offsets =
        reinterpret_cast<const uint64_t *>(args->remote_request_offsets_address);

    for (uint32_t completed = 0U; completed < args->iterations;) {
        const uint32_t request_count =
            args->iterations - completed < requests_per_window ? args->iterations - completed : requests_per_window;
        const uint64_t window_wrs = static_cast<uint64_t>(request_count) * chunks_per_request;
        if (post_mode == NDS_DEVICE_BENCHMARK_POST_LINKED) {
            for (uint64_t window_wr_index = 0U; window_wr_index < window_wrs;) {
                const uint32_t batch_count =
                    static_cast<uint32_t>(window_wrs - window_wr_index > kMaxLinkedWrs
                                              ? linked_wrs
                                              : window_wrs - window_wr_index);
                NdsDeviceSendWr wrs[kMaxLinkedWrs]{};
                for (uint32_t batch_index = 0U; batch_index < batch_count; ++batch_index) {
                    const uint64_t index = window_wr_index + batch_index;
                    const uint32_t request = static_cast<uint32_t>(index / chunks_per_request);
                    const uint64_t chunk_index = index % chunks_per_request;
                    const uint64_t request_index = static_cast<uint64_t>(completed) + request;
                    const uint64_t request_offset = request_offsets == nullptr
                                                        ? static_cast<uint64_t>(request_index % args->in_flight) * args->bytes
                                                        : request_offsets[args->request_offset_start + request_index];
                    const uint64_t remote_request_offset = remote_request_offsets == nullptr
                                                               ? request_offset
                                                               : remote_request_offsets[args->request_offset_start + request_index];
                    const uint64_t chunk_offset = chunk_index * chunk_bytes;
                    const uint32_t length = chunk_offset + chunk_bytes > args->bytes
                                                ? args->bytes - static_cast<uint32_t>(chunk_offset)
                                                : chunk_bytes;
                    wrs[batch_index] = {
                        1U + result.wqe_count + batch_index,
                        args->operation == NDS_DEVICE_BENCHMARK_READ ? NDS_DEVICE_WR_RDMA_READ
                                                                       : NDS_DEVICE_WR_RDMA_WRITE,
                        (signal_every != 0U && ((index + 1U) % signal_every == 0U || index + 1U == window_wrs)) ||
                                (signal_every == 0U && index + 1U == window_wrs)
                            ? static_cast<uint32_t>(NDS_DEVICE_SEND_SIGNALED)
                            : 0U,
                        {args->local_address + request_offset + chunk_offset, length, args->local_key},
                        args->remote_address + remote_request_offset + chunk_offset,
                        args->remote_key,
                        0U};
                }
                int32_t post_result = -1;
                const uint32_t post_status =
                    NdsAicpuPostSendListImpl(&args->transport.control_qp, wrs, batch_count, &post_result);
                if (post_status != kNdsAicpuSuccess || post_result != 0)
                    return fail(args, NDS_DEVICE_BENCHMARK_POST_FAILED, result);
                result.wqe_count += batch_count;
                window_wr_index += batch_count;
            }
        } else {
            for (uint32_t request = 0U; request < request_count; ++request) {
                const uint64_t request_index = static_cast<uint64_t>(completed) + request;
                const uint64_t request_offset = request_offsets == nullptr
                                                    ? static_cast<uint64_t>(request_index % args->in_flight) * args->bytes
                                                    : request_offsets[args->request_offset_start + request_index];
                const uint64_t remote_request_offset = remote_request_offsets == nullptr
                                                           ? request_offset
                                                           : remote_request_offsets[args->request_offset_start + request_index];
                const uint64_t local_base = args->local_address + request_offset;
                const uint64_t remote_base = args->remote_address + remote_request_offset;
                for (uint64_t chunk_offset = 0U; chunk_offset < args->bytes; chunk_offset += chunk_bytes) {
                    const uint32_t length =
                        chunk_offset + chunk_bytes > args->bytes ? args->bytes - static_cast<uint32_t>(chunk_offset)
                                                                  : chunk_bytes;
                    const uint64_t wr_index = result.wqe_count;
                    const uint64_t window_wr_index = static_cast<uint64_t>(request) * chunks_per_request +
                                                     chunk_offset / chunk_bytes;
                    const NdsDeviceSendWr wr{
                        1U + wr_index,
                        args->operation == NDS_DEVICE_BENCHMARK_READ ? NDS_DEVICE_WR_RDMA_READ
                                                                       : NDS_DEVICE_WR_RDMA_WRITE,
                        (signal_every != 0U && ((window_wr_index + 1U) % signal_every == 0U ||
                                                window_wr_index + 1U == window_wrs)) ||
                                (signal_every == 0U && window_wr_index + 1U == window_wrs)
                            ? static_cast<uint32_t>(NDS_DEVICE_SEND_SIGNALED)
                            : 0U,
                        {local_base + chunk_offset, length, args->local_key},
                        remote_base + chunk_offset,
                        args->remote_key,
                        0U};
                    int32_t post_result = -1;
                    const uint32_t post_status = args->operation == NDS_DEVICE_BENCHMARK_READ
                                                      ? NdsAicpuRdmaReadImpl(&args->transport, &wr, &post_result)
                                                      : NdsAicpuRdmaWriteImpl(&args->transport, &wr, &post_result);
                    if (post_status != kNdsAicpuSuccess || post_result != 0)
                        return fail(args, NDS_DEVICE_BENCHMARK_POST_FAILED, result);
                    ++result.wqe_count;
                }
            }
        }

        const uint64_t expected_completions =
            signal_every == 0U ? 1U : (window_wrs + signal_every - 1U) / signal_every;
        uint64_t completed_wrs{};
        uint32_t idle_polls{};
        while (completed_wrs < expected_completions) {
            NdsDeviceWc completions[NDS_DEVICE_MAX_COMPLETIONS]{};
            int32_t poll_result = -1;
            const uint32_t poll_status = NdsAicpuPollCqImpl(&args->transport.control_qp, 1U, poll_batch,
                                                             completions, &poll_result);
            ++result.poll_count;
            if (poll_status != kNdsAicpuSuccess || poll_result < 0)
                return fail(args, NDS_DEVICE_BENCHMARK_POLL_FAILED, result);
            if (poll_result == 0) {
                if (++idle_polls >= kMaxIdlePolls)
                    return fail(args, NDS_DEVICE_BENCHMARK_POLL_TIMEOUT, result);
                continue;
            }
            idle_polls = 0U;
            for (int32_t index = 0; index < poll_result; ++index) {
                if (completions[index].status != 0)
                {
                    result.completion_status = completions[index].status;
                    result.completion_vendor_error = completions[index].vendor_error;
                    return fail(args, NDS_DEVICE_BENCHMARK_COMPLETION_FAILED, result);
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
    return kNdsAicpuSuccess;
}

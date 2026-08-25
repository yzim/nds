#ifndef NDS_DEVICE_BENCHMARK_H
#define NDS_DEVICE_BENCHMARK_H

#include "nds/device_transport.h"

#include <stdint.h>

enum NdsDeviceBenchmarkOperation {
    NDS_DEVICE_BENCHMARK_READ = 1U,
    NDS_DEVICE_BENCHMARK_WRITE = 2U,
};

enum NdsDeviceBenchmarkStatus {
    NDS_DEVICE_BENCHMARK_SUCCESS = 0U,
    NDS_DEVICE_BENCHMARK_INVALID_ARGUMENT = 1U,
    NDS_DEVICE_BENCHMARK_POST_FAILED = 2U,
    NDS_DEVICE_BENCHMARK_POLL_FAILED = 3U,
    NDS_DEVICE_BENCHMARK_POLL_TIMEOUT = 4U,
    NDS_DEVICE_BENCHMARK_COMPLETION_FAILED = 5U,
};

enum NdsDeviceBenchmarkPostMode {
    NDS_DEVICE_BENCHMARK_POST_INDIVIDUAL = 0U,
    NDS_DEVICE_BENCHMARK_POST_LINKED = 1U,
    NDS_DEVICE_BENCHMARK_POST_RAW_SQ = 2U,
};

/* post_mode packs backend-specific benchmark controls without changing the launch ABI.
 * AICPU uses the low byte for NdsDeviceBenchmarkPostMode. AIV uses that byte
 * as its doorbell interval; both backends use the signal interval byte. AICPU
 * raw-SQ mode uses the linked-WR byte as its deferred-MMIO interval because
 * linked-WR count is unused in that mode. */
enum {
    NDS_DEVICE_BENCHMARK_POST_MODE_MASK = 0xffU,
    NDS_DEVICE_BENCHMARK_POLL_BATCH_SHIFT = 8U,
    NDS_DEVICE_BENCHMARK_SIGNAL_EVERY_SHIFT = 16U,
    NDS_DEVICE_BENCHMARK_LINKED_WRS_SHIFT = 24U,
};

typedef struct NdsDeviceRdmaBenchmarkArgs {
    NdsDeviceTransport transport;
    uint64_t local_address;
    uint64_t remote_address;
    uint32_t local_key;
    uint32_t remote_key;
    uint32_t bytes;
    uint32_t iterations;
    uint32_t in_flight;
    uint32_t max_wr_bytes;
    uint32_t max_wrs_per_window;
    uint32_t operation;
    uint64_t result_address;
    uint64_t request_offsets_address;
    uint64_t remote_request_offsets_address;
    uint64_t request_offset_start;
    int32_t return_value;
    uint32_t post_mode;
} NdsDeviceRdmaBenchmarkArgs;

typedef struct NdsDeviceRdmaBenchmarkResult {
    uint32_t status;
    uint32_t reserved;
    uint64_t operations_completed;
    uint64_t bytes_transferred;
    uint64_t wqe_count;
    uint64_t poll_count;
    int32_t completion_status;
    uint32_t completion_vendor_error;
} NdsDeviceRdmaBenchmarkResult;

#if defined(__cplusplus)
static_assert(sizeof(NdsDeviceRdmaBenchmarkArgs) == 320, "device RDMA benchmark args ABI changed");
static_assert(sizeof(NdsDeviceRdmaBenchmarkResult) == 48, "device RDMA benchmark result ABI changed");
#else
_Static_assert(sizeof(NdsDeviceRdmaBenchmarkArgs) == 320, "device RDMA benchmark args ABI changed");
_Static_assert(sizeof(NdsDeviceRdmaBenchmarkResult) == 48, "device RDMA benchmark result ABI changed");
#endif

#endif

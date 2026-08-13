#ifndef NDS_STORAGE_PROTOCOL_H
#define NDS_STORAGE_PROTOCOL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NDS_STORAGE_BOOTSTRAP_MAGIC UINT32_C(0x4e445342) /* "NDSB" */
#define NDS_STORAGE_COMMAND_MAGIC UINT32_C(0x4e445343) /* "NDSC" */
#define NDS_STORAGE_COMPLETION_MAGIC UINT32_C(0x4e445344) /* "NDSD" */
#define NDS_STORAGE_PROTOCOL_VERSION UINT16_C(1)
#define NDS_STORAGE_ERROR_CAPACITY 256U

enum nds_storage_access {
    NDS_STORAGE_ACCESS_REMOTE_WRITE = UINT32_C(0x00000002),
    NDS_STORAGE_ACCESS_REMOTE_READ = UINT32_C(0x00000004),
};

enum nds_storage_operation {
    NDS_STORAGE_READ = 1,
    NDS_STORAGE_WRITE = 2,
};

enum nds_storage_completion_state {
    NDS_STORAGE_COMPLETION_PENDING = 0,
    NDS_STORAGE_COMPLETION_COMPLETE = 1,
};

enum nds_storage_status {
    NDS_STORAGE_SUCCESS = 0,
    NDS_STORAGE_INVALID_COMMAND = 1,
    NDS_STORAGE_RANGE_ERROR = 2,
    NDS_STORAGE_TRANSPORT_ERROR = 3,
    NDS_STORAGE_INTERNAL_ERROR = 4,
};

typedef struct nds_storage_memory {
    uint64_t address;
    uint64_t length;
    uint32_t rkey;
    uint32_t access;
} nds_storage_memory;

/* TCP bootstrap record. The completion MR is session-static. */
typedef struct nds_storage_bootstrap {
    uint64_t namespace_capacity;
    nds_storage_memory completion;
} nds_storage_bootstrap;

/* NPU-to-CPU RDMA Send payload. Data memory is application-owned per command. */
typedef struct nds_storage_command {
    uint64_t request_id;
    uint16_t operation;
    uint64_t offset;
    uint64_t length;
    nds_storage_memory data;
} nds_storage_command;

/* CPU-to-NPU one-sided completion flag payload. */
typedef struct nds_storage_completion {
    uint64_t request_id;
    uint16_t state;
    uint16_t status;
    uint64_t bytes_transferred;
} nds_storage_completion;

typedef struct __attribute__((packed)) nds_storage_bootstrap_wire {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved_0;
    uint64_t namespace_capacity;
    uint64_t completion_address;
    uint64_t completion_length;
    uint32_t completion_rkey;
    uint32_t completion_access;
    uint8_t reserved[8];
} nds_storage_bootstrap_wire;

typedef struct __attribute__((packed)) nds_storage_command_wire {
    uint32_t magic;
    uint16_t version;
    uint16_t operation;
    uint64_t request_id;
    uint64_t offset;
    uint64_t length;
    uint64_t data_address;
    uint64_t data_length;
    uint32_t data_rkey;
    uint32_t data_access;
    uint8_t reserved[8];
} nds_storage_command_wire;

typedef struct __attribute__((packed)) nds_storage_completion_wire {
    uint32_t magic;
    uint16_t version;
    uint16_t state;
    uint16_t status;
    uint16_t reserved_0;
    uint64_t request_id;
    uint64_t bytes_transferred;
    uint8_t reserved[4];
} nds_storage_completion_wire;

#ifdef __cplusplus
static_assert(sizeof(nds_storage_bootstrap_wire) == 48, "NDS storage bootstrap wire ABI must remain 48 bytes");
static_assert(sizeof(nds_storage_command_wire) == 64, "NDS storage command wire ABI must remain 64 bytes");
static_assert(sizeof(nds_storage_completion_wire) == 32, "NDS storage completion wire ABI must remain 32 bytes");
#else
_Static_assert(sizeof(nds_storage_bootstrap_wire) == 48, "NDS storage bootstrap wire ABI must remain 48 bytes");
_Static_assert(sizeof(nds_storage_command_wire) == 64, "NDS storage command wire ABI must remain 64 bytes");
_Static_assert(sizeof(nds_storage_completion_wire) == 32, "NDS storage completion wire ABI must remain 32 bytes");
#endif

int nds_storage_bootstrap_encode(const nds_storage_bootstrap *bootstrap, nds_storage_bootstrap_wire *wire,
                                 char error[NDS_STORAGE_ERROR_CAPACITY]);
int nds_storage_bootstrap_decode(const nds_storage_bootstrap_wire *wire, nds_storage_bootstrap *bootstrap,
                                 char error[NDS_STORAGE_ERROR_CAPACITY]);
int nds_storage_command_encode(const nds_storage_command *command, nds_storage_command_wire *wire,
                               char error[NDS_STORAGE_ERROR_CAPACITY]);
int nds_storage_command_decode(const nds_storage_command_wire *wire, nds_storage_command *command,
                               char error[NDS_STORAGE_ERROR_CAPACITY]);
int nds_storage_completion_encode(const nds_storage_completion *completion, nds_storage_completion_wire *wire,
                                  char error[NDS_STORAGE_ERROR_CAPACITY]);
int nds_storage_completion_decode(const nds_storage_completion_wire *wire, nds_storage_completion *completion,
                                  char error[NDS_STORAGE_ERROR_CAPACITY]);

#ifdef __cplusplus
}
#endif

#endif

#ifndef NDS_PROTOCOL_H
#define NDS_PROTOCOL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NDS_PROTOCOL_BOOTSTRAP_MAGIC UINT32_C(0x4e445342) /* "NDSB" */
#define NDS_PROTOCOL_COMMAND_MAGIC UINT32_C(0x4e445343) /* "NDSC" */
#define NDS_PROTOCOL_COMPLETION_MAGIC UINT32_C(0x4e445344) /* "NDSD" */
#define NDS_PROTOCOL_NAMESPACE_MAGIC UINT32_C(0x4e44534e) /* "NDSN" */
#define NDS_PROTOCOL_VERSION UINT16_C(1)
#define NDS_PROTOCOL_ERROR_CAPACITY 256U

enum nds_protocol_access {
    NDS_PROTOCOL_ACCESS_REMOTE_WRITE = UINT32_C(0x00000002),
    NDS_PROTOCOL_ACCESS_REMOTE_READ = UINT32_C(0x00000004),
};

enum nds_protocol_operation {
    NDS_PROTOCOL_READ = 1,
    NDS_PROTOCOL_WRITE = 2,
};

enum nds_protocol_completion_state {
    NDS_PROTOCOL_COMPLETION_PENDING = 0,
    NDS_PROTOCOL_COMPLETION_COMPLETE = 1,
};

enum nds_protocol_status {
    NDS_PROTOCOL_SUCCESS = 0,
    NDS_PROTOCOL_INVALID_COMMAND = 1,
    NDS_PROTOCOL_RANGE_ERROR = 2,
    NDS_PROTOCOL_TRANSPORT_ERROR = 3,
    NDS_PROTOCOL_INTERNAL_ERROR = 4,
};

typedef struct nds_protocol_memory {
    uint64_t address;
    uint64_t length;
    uint32_t rkey;
    uint32_t access;
} nds_protocol_memory;

/* TCP bootstrap record. The completion MR is session-static. */
typedef struct nds_protocol_bootstrap {
    nds_protocol_memory completion;
} nds_protocol_bootstrap;

typedef struct nds_protocol_namespace {
    uint64_t capacity;
} nds_protocol_namespace;

/* NPU-to-CPU RDMA Send payload. Data memory is application-owned per command. */
typedef struct nds_protocol_command {
    uint64_t request_id;
    uint16_t operation;
    uint64_t offset;
    uint64_t length;
    nds_protocol_memory data;
} nds_protocol_command;

/* CPU-to-NPU one-sided completion flag payload. */
typedef struct nds_protocol_completion {
    uint64_t request_id;
    uint16_t state;
    uint16_t status;
    uint64_t bytes_transferred;
} nds_protocol_completion;

typedef struct __attribute__((packed)) nds_protocol_bootstrap_wire {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved_0;
    uint64_t completion_address;
    uint64_t completion_length;
    uint32_t completion_rkey;
    uint32_t completion_access;
    uint8_t reserved[8];
} nds_protocol_bootstrap_wire;

typedef struct __attribute__((packed)) nds_protocol_namespace_wire {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved_0;
    uint64_t capacity;
} nds_protocol_namespace_wire;

typedef struct __attribute__((packed)) nds_protocol_command_wire {
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
} nds_protocol_command_wire;

typedef struct __attribute__((packed)) nds_protocol_completion_wire {
    uint32_t magic;
    uint16_t version;
    uint16_t state;
    uint16_t status;
    uint16_t reserved_0;
    uint64_t request_id;
    uint64_t bytes_transferred;
    uint8_t reserved[4];
} nds_protocol_completion_wire;

#ifdef __cplusplus
static_assert(sizeof(nds_protocol_bootstrap_wire) == 40, "NDS protocol bootstrap wire ABI must remain 40 bytes");
static_assert(sizeof(nds_protocol_namespace_wire) == 16, "NDS protocol namespace wire ABI must remain 16 bytes");
static_assert(sizeof(nds_protocol_command_wire) == 64, "NDS protocol command wire ABI must remain 64 bytes");
static_assert(sizeof(nds_protocol_completion_wire) == 32, "NDS protocol completion wire ABI must remain 32 bytes");
#else
_Static_assert(sizeof(nds_protocol_bootstrap_wire) == 40, "NDS protocol bootstrap wire ABI must remain 40 bytes");
_Static_assert(sizeof(nds_protocol_namespace_wire) == 16, "NDS protocol namespace wire ABI must remain 16 bytes");
_Static_assert(sizeof(nds_protocol_command_wire) == 64, "NDS protocol command wire ABI must remain 64 bytes");
_Static_assert(sizeof(nds_protocol_completion_wire) == 32, "NDS protocol completion wire ABI must remain 32 bytes");
#endif

int nds_protocol_bootstrap_encode(const nds_protocol_bootstrap *bootstrap, nds_protocol_bootstrap_wire *wire,
                                 char error[NDS_PROTOCOL_ERROR_CAPACITY]);
int nds_protocol_bootstrap_decode(const nds_protocol_bootstrap_wire *wire, nds_protocol_bootstrap *bootstrap,
                                 char error[NDS_PROTOCOL_ERROR_CAPACITY]);
int nds_protocol_namespace_encode(const nds_protocol_namespace *namespace_record, nds_protocol_namespace_wire *wire,
                                 char error[NDS_PROTOCOL_ERROR_CAPACITY]);
int nds_protocol_namespace_decode(const nds_protocol_namespace_wire *wire, nds_protocol_namespace *namespace_record,
                                 char error[NDS_PROTOCOL_ERROR_CAPACITY]);
int nds_protocol_command_encode(const nds_protocol_command *command, nds_protocol_command_wire *wire,
                               char error[NDS_PROTOCOL_ERROR_CAPACITY]);
int nds_protocol_command_decode(const nds_protocol_command_wire *wire, nds_protocol_command *command,
                               char error[NDS_PROTOCOL_ERROR_CAPACITY]);
int nds_protocol_completion_encode(const nds_protocol_completion *completion, nds_protocol_completion_wire *wire,
                                  char error[NDS_PROTOCOL_ERROR_CAPACITY]);
int nds_protocol_completion_decode(const nds_protocol_completion_wire *wire, nds_protocol_completion *completion,
                                  char error[NDS_PROTOCOL_ERROR_CAPACITY]);

#ifdef __cplusplus
}
#endif

#endif

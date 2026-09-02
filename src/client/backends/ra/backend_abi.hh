#ifndef NDS_CLIENT_RA_BACKEND_ABI_HH
#define NDS_CLIENT_RA_BACKEND_ABI_HH

#include "backend_transport.h"
#include "backend_storage.h"

#include <cstdint>

/* The RA artifact consumes the same envelopes used by AIV/AICPU kernels. */
using NdsRaBackendPostSend = int (*)(const NdsQpDescriptor *, const NdsSendWr *, void *stream);
using NdsRaBackendPostRecv = int (*)(const NdsQpDescriptor *, const NdsRecvWr *);
using NdsRaBackendPollCq = int (*)(const NdsQpDescriptor *, std::uint32_t, std::uint32_t, NdsWc *);
using NdsRaBackendRdmaSend = int (*)(const NdsTransportDescriptor *, std::uint32_t, const NdsSendWr *);
using NdsRaBackendRdmaRecv = int (*)(const NdsTransportDescriptor *, std::uint32_t, const NdsRecvWr *);
using NdsRaBackendRdmaRead = int (*)(const NdsTransportDescriptor *, std::uint32_t, const NdsSendWr *);
using NdsRaBackendRdmaWrite = int (*)(const NdsTransportDescriptor *, std::uint32_t, const NdsSendWr *);
using NdsRaBackendStorageRead = int (*)(const NdsStorageDescriptor *, const nds::StorageReadCommand *);
using NdsRaBackendStorageWrite = int (*)(const NdsStorageDescriptor *, const nds::StorageWriteCommand *);
using NdsRaBackendStorageReadBatch = int (*)(const NdsStorageDescriptor *, const nds::StorageBatchReadCommand *);
using NdsRaBackendStorageWriteBatch = int (*)(const NdsStorageDescriptor *, const nds::StorageBatchWriteCommand *);

#endif

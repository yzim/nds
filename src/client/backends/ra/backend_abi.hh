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
using NdsRaBackendStorageBootstrap = int (*)(const NdsStorageBootstrapDescriptor *);
using NdsRaBackendStorageRead = int (*)(const NdsStorageOperationArgs *);
using NdsRaBackendStorageWrite = int (*)(const NdsStorageOperationArgs *);
using NdsRaBackendStorageReadBatch = int (*)(const NdsStorageBatchOperationArgs *);
using NdsRaBackendStorageWriteBatch = int (*)(const NdsStorageBatchOperationArgs *);
using NdsRaBackendStorageWait = int (*)(const NdsStorageDescriptor *, std::uint32_t);

#endif

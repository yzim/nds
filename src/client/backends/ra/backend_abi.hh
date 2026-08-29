#ifndef NDS_CLIENT_RA_BACKEND_ABI_HH
#define NDS_CLIENT_RA_BACKEND_ABI_HH

#include "device_verbs.h"
#include "device_storage.h"
#include "device_transport.h"

#include <cstdint>

/* The RA artifact consumes the same envelopes used by AIV/AICPU kernels. */
using NdsRaBackendPostSend = int (*)(const NdsDeviceQp *, const NdsDeviceSendWr *);
using NdsRaBackendPollCq = int (*)(const NdsDeviceQp *, std::uint32_t, std::uint32_t, NdsDeviceWc *);
using NdsRaBackendRdmaSend = int (*)(const NdsDeviceTransport *, const NdsDeviceSendWr *);
using NdsRaBackendRdmaRecv = int (*)(const NdsDeviceTransport *, const NdsDeviceRecvWr *);
using NdsRaBackendStorage = int (*)(const NdsDeviceStorageContext *, const nds::StorageReadCommand *);
using NdsRaBackendStorageWrite = int (*)(const NdsDeviceStorageContext *, const nds::StorageWriteCommand *);
using NdsRaBackendStorageBatchRead = int (*)(const NdsDeviceStorageContext *, const nds::StorageBatchReadCommand *);
using NdsRaBackendStorageBatchWrite = int (*)(const NdsDeviceStorageContext *, const nds::StorageBatchWriteCommand *);

#endif

#ifndef NDS_CLIENT_RA_BACKEND_ABI_HH
#define NDS_CLIENT_RA_BACKEND_ABI_HH

#include "device_verbs.h"

#include <cstdint>

/* The RA artifact consumes the same envelopes used by AIV/AICPU kernels. */
using NdsRaBackendPostSend = int (*)(const NdsDeviceQp *, const NdsDeviceSendWr *, void *stream);
using NdsRaBackendPostRecv = int (*)(const NdsDeviceQp *, const NdsDeviceRecvWr *);
using NdsRaBackendPollCq = int (*)(const NdsDeviceQp *, std::uint32_t, std::uint32_t, NdsDeviceWc *);

#endif

#ifndef NDS_CLIENT_BACKENDS_BACKEND_MODE_HH
#define NDS_CLIENT_BACKENDS_BACKEND_MODE_HH

namespace nds::client {

/* Selects the implementation used by BackendLauncher. */
enum class BackendMode {
    Ra,
    Aicpu,
    Aiv,
};

}  // namespace nds::client

#endif

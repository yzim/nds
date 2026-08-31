#ifndef NDS_CLIENT_BACKEND_RA_LAUNCHER_HH
#define NDS_CLIENT_BACKEND_RA_LAUNCHER_HH

#include "result.hh"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include "device_verbs.h"

namespace nds {

/* Host-side wrapper for the dynamically loaded RA verbs entry points. */
class RaLauncher {
public:
    RaLauncher();
    ~RaLauncher();
    RaLauncher(const RaLauncher &) = delete;
    RaLauncher &operator=(const RaLauncher &) = delete;
    RaLauncher(RaLauncher &&) noexcept = default;
    RaLauncher &operator=(RaLauncher &&) noexcept = default;

    Result<void> load(const std::string &backend_path);
    Result<void> post_send(const NdsDeviceQp &qp, const NdsDeviceSendWr &wr, void *stream);
    Result<void> post_recv(const NdsDeviceQp &qp, const NdsDeviceRecvWr &wr);
    Result<std::uint32_t> poll_cq(const NdsDeviceQp &qp, std::uint32_t send_cq, std::uint32_t max_completions,
                                  NdsDeviceWc *wc);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace nds

#endif

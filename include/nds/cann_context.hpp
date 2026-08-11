#ifndef NDS_CANN_CONTEXT_HPP
#define NDS_CANN_CONTEXT_HPP

#include "nds/acl_loader.h"
#include "nds/hcomm_loader.h"
#include "nds/tsd_loader.h"

#include <cstdint>
#include <string>

namespace nds {

/* Paths are explicit so the application remains distributable across CANN installations. */
struct CannContextConfig {
    std::string ascendcl_library;
    std::string tsdclient_library;
    std::string hcomm_library;
    std::string rank_table_path;
    std::string identify{"nds"};
    std::uint32_t logical_device_id{0};
    std::uint32_t rank_size{2};
    bool preopen_tsd{false};
};

/*
 * Owns the CANN process/device and HCOMM bootstrap lifecycle.  A successful
 * instance leaves HCOMM as the owner of global HCCL/HCCP/RA initialization.
 * NDS RA resources begin only after this class has initialized successfully.
 */
class CannContext {
public:
    CannContext() = default;
    ~CannContext();
    CannContext(const CannContext &) = delete;
    CannContext &operator=(const CannContext &) = delete;
    CannContext(CannContext &&) = delete;
    CannContext &operator=(CannContext &&) = delete;

    bool initialize(const CannContextConfig &config);
    void reset() noexcept;
    bool initialized() const noexcept;
    const std::string &error() const noexcept;
    const CannContextConfig &config() const noexcept;

private:
    void set_error(const std::string &message);

    CannContextConfig config_{};
    nds_acl_api acl_{};
    nds_tsd_api tsd_{};
    nds_hcomm_api hcomm_{};
    bool acl_initialized_{false};
    bool tsd_opened_{false};
    bool hcomm_initialized_{false};
    bool initialized_{false};
    std::string error_;
};

} // namespace nds

#endif

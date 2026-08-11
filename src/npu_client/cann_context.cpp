#include "nds/cann_context.hpp"

#include <array>
#include <filesystem>
#include <system_error>
#include <utility>


namespace nds {
namespace {

bool validate_cann_library_paths(const CannContextConfig &config, std::string &error)
{
    namespace fs = std::filesystem;
    const std::array<std::pair<const char *, const std::string *>, 3> libraries{{
        {"AscendCL", &config.ascendcl_library},
        {"TSD client", &config.tsdclient_library},
        {"HCOMM", &config.hcomm_library},
    }};
    fs::path selected_directory;

    for (const auto &[name, value] : libraries) {
        const fs::path library_path(*value);
        std::error_code ec;

        if (!library_path.is_absolute()) {
            error = std::string(name) + " library path must be absolute";
            return false;
        }
        if (!fs::exists(library_path, ec) || ec) {
            /* Let the specific loader report the authoritative dlopen error. */
            continue;
        }
        if (!fs::is_regular_file(library_path, ec) || ec) {
            error = std::string(name) + " library path is not a regular file: " + library_path.string();
            return false;
        }

        const fs::path canonical_directory = fs::weakly_canonical(library_path, ec).parent_path();
        if (ec) {
            error = std::string("cannot canonicalize ") + name + " library path: " + ec.message();
            return false;
        }
        if (selected_directory.empty()) {
            selected_directory = canonical_directory;
        } else if (canonical_directory != selected_directory) {
            error = "CANN libraries must come from one directory; found " + selected_directory.string() +
                    " and " + canonical_directory.string();
            return false;
        }
    }
    return true;
}

} // namespace

CannContext::~CannContext()
{
    reset();
}

void CannContext::set_error(const std::string &message)
{
    error_ = message;
}

bool CannContext::initialize(const CannContextConfig &config)
{
    int acl_result;
    std::uint32_t tsd_result;
    int capability = 0;

    if (initialized_) {
        set_error("CANN context is already initialized");
        return false;
    }
    if (config.ascendcl_library.empty() || config.tsdclient_library.empty() || config.hcomm_library.empty() ||
        config.rank_table_path.empty() || config.identify.empty() || config.rank_size < 2U) {
        set_error("CANN context requires library paths, a rank table, a non-empty identifier, and rank size >= 2");
        return false;
    }
    if (!validate_cann_library_paths(config, error_)) {
        return false;
    }
    config_ = config;

    if (nds_acl_open(&acl_, config_.ascendcl_library.c_str()) != 0) {
        set_error(std::string("cannot load AscendCL: ") + nds_acl_error(&acl_));
        goto fail;
    }
    acl_result = acl_.init(nullptr);
    if (acl_result != 0) {
        set_error("aclInit failed: " + std::to_string(acl_result));
        goto fail;
    }
    acl_initialized_ = true;
    acl_result = acl_.set_device(static_cast<std::int32_t>(config_.logical_device_id));
    if (acl_result != 0) {
        set_error("aclrtSetDevice failed: " + std::to_string(acl_result));
        goto fail;
    }

    if (nds_tsd_open_library(&tsd_, config_.tsdclient_library.c_str()) != 0) {
        set_error(std::string("cannot load TSD client: ") + nds_tsd_error(&tsd_));
        goto fail;
    }
    tsd_result = tsd_.capability_get(config_.logical_device_id, NDS_TSD_CAPABILITY_MULTIPLE_HCCP,
                                     reinterpret_cast<std::uint64_t>(&capability));
    if (tsd_result != 0U) {
        set_error("TsdCapabilityGet(MULTIPLE_HCCP) failed: " + std::to_string(tsd_result));
        goto fail;
    }
    if (capability == 0) {
        set_error("TSD does not report process-granular HCCP support");
        goto fail;
    }
    if (config_.preopen_tsd) {
        tsd_result = tsd_.open(config_.logical_device_id, config_.rank_size);
        if (tsd_result != 0U) {
            set_error("TsdOpen failed: " + std::to_string(tsd_result));
            goto fail;
        }
        tsd_opened_ = true;
    }

    if (nds_hcomm_open(&hcomm_, config_.hcomm_library.c_str()) != 0) {
        set_error(std::string("cannot load HCOMM: ") + nds_hcomm_error(&hcomm_));
        goto fail;
    }
    acl_result = hcomm_.init_by_file(config_.rank_table_path.c_str(), config_.identify.c_str());
    if (acl_result != 0) {
        set_error("HcomInitByFile failed: " + std::to_string(acl_result));
        goto fail;
    }
    hcomm_initialized_ = true;
    initialized_ = true;
    error_.clear();
    return true;

fail:
    reset();
    return false;
}

void CannContext::reset() noexcept
{
    if (hcomm_initialized_) {
        (void)hcomm_.destroy();
        hcomm_initialized_ = false;
    }
    nds_hcomm_close(&hcomm_);
    if (tsd_opened_) {
        (void)tsd_.close(config_.logical_device_id);
        tsd_opened_ = false;
    }
    nds_tsd_close_library(&tsd_);
    if (acl_initialized_) {
        (void)acl_.finalize();
        acl_initialized_ = false;
    }
    nds_acl_close(&acl_);
    initialized_ = false;
}

bool CannContext::initialized() const noexcept
{
    return initialized_;
}

const std::string &CannContext::error() const noexcept
{
    return error_;
}

const CannContextConfig &CannContext::config() const noexcept
{
    return config_;
}

} // namespace nds

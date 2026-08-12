#include "nds/npu_ra_context.hpp"
#include "nds/npu_ra_qp.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

namespace {

constexpr std::uint64_t kDefaultSize = 4096U;

struct Config {
    nds::NpuRaContextConfig context{};
    nds::NpuRaQpConfig qp{};
    std::uint64_t size{kDefaultSize};
    bool execute{false};
};

void usage(const char *program)
{
    std::cerr << "usage: " << program
              << " --ascendcl ABS_PATH --runtime ABS_PATH --ra ABS_PATH --npu-ip IPV4"
              << " --logical-device ID --physical-device ID --execute [--size BYTES]\n"
              << "\nRegisters one small NPU allocation with RA, prints its keys, deregisters it, and exits. "
              << "No peer connection or work request is created.\n";
}

bool parse_u64(const char *text, std::uint64_t *value)
{
    char *end = nullptr;
    unsigned long long parsed;

    if (text == nullptr || value == nullptr) return false;
    errno = 0;
    parsed = std::strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0') return false;
    *value = static_cast<std::uint64_t>(parsed);
    return true;
}

bool parse_args(int argc, char **argv, Config *config)
{
    for (int index = 1; index < argc; ++index) {
        const std::string argument(argv[index]);
        auto value = [&]() -> const char * { return ++index < argc ? argv[index] : nullptr; };
        std::uint64_t parsed = 0;

        if (argument == "--execute") {
            config->execute = true;
        } else if (argument == "--ascendcl") {
            const char *text = value(); if (text == nullptr) return false; config->context.ascendcl_library = text;
        } else if (argument == "--runtime") {
            const char *text = value(); if (text == nullptr) return false; config->context.runtime_library = text;
        } else if (argument == "--ra") {
            const char *text = value(); if (text == nullptr) return false; config->context.ra_library = text;
        } else if (argument == "--npu-ip") {
            const char *text = value(); if (text == nullptr) return false; config->qp.local_ipv4 = text;
        } else if (argument == "--logical-device") {
            const char *text = value(); if (text == nullptr || !parse_u64(text, &parsed) ||
                parsed > std::numeric_limits<std::uint32_t>::max()) return false;
            config->context.logical_device_id = static_cast<std::uint32_t>(parsed);
        } else if (argument == "--physical-device") {
            const char *text = value(); if (text == nullptr || !parse_u64(text, &parsed) ||
                parsed > std::numeric_limits<std::uint32_t>::max()) return false;
            config->context.physical_device_id = static_cast<std::uint32_t>(parsed);
            config->qp.physical_device_id = static_cast<std::uint32_t>(parsed);
        } else if (argument == "--size") {
            const char *text = value();
            if (text == nullptr || !parse_u64(text, &config->size) || config->size == 0U) return false;
        } else {
            return false;
        }
    }
    return config->execute && !config->context.ascendcl_library.empty() && !config->context.runtime_library.empty() &&
           !config->context.ra_library.empty() && !config->qp.local_ipv4.empty();
}

} // namespace

int main(int argc, char **argv)
{
    Config config;
    nds::NpuRaContext context;
    nds::NpuRaQp qp;
    void *device_memory = nullptr;
    void *mr_handle = nullptr;
    nds_ra_mr_info mr{};
    nds_ra_completion completion{};
    int exit_code = EXIT_FAILURE;

    if (!parse_args(argc, argv, &config)) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }
    if (!context.initialize(config.context)) {
        std::cerr << "context initialization failed: " << context.error() << '\n';
        return EXIT_FAILURE;
    }
    if (!qp.create(context.ra_api(), config.qp)) {
        std::cerr << "RA rdev/QP creation failed: " << qp.error() << '\n';
        return EXIT_FAILURE;
    }
    const int polled = qp.poll_send_completions(&completion, 1U);
    if (polled < 0) {
        std::cerr << "send-CQ probe failed: " << qp.error() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "RA send CQ is available; empty poll returned " << polled << " completion(s).\n";
    if (!context.allocate_device_memory(static_cast<std::size_t>(config.size), &device_memory)) {
        std::cerr << "device allocation failed: " << context.error() << '\n';
        return EXIT_FAILURE;
    }
    if (!qp.register_memory(device_memory, config.size, NDS_RA_ACCESS_DIRECT_NPU, mr, &mr_handle)) {
        std::cerr << "RA memory registration failed: " << qp.error() << '\n';
        (void)context.free_device_memory(device_memory);
        return EXIT_FAILURE;
    }

    std::cout << "RA MR registered: address=" << device_memory << " size=" << config.size
              << " lkey=" << mr.local_key << " rkey=" << mr.remote_key
              << " access=LOCAL_WRITE|REMOTE_WRITE|REMOTE_READ\n";

    if (!qp.deregister_memory(mr_handle)) {
        std::cerr << "RA memory deregistration failed: " << qp.error() << '\n';
    } else if (!context.free_device_memory(device_memory)) {
        std::cerr << "device free failed: " << context.error() << '\n';
    } else {
        std::cout << "RA MR lifecycle completed cleanly.\n";
        exit_code = EXIT_SUCCESS;
    }
    return exit_code;
}

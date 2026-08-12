#include "nds/acl_loader.h"
#include "nds/aicpu_roce.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

int main(int argc, char **argv)
{
    nds_acl_api acl{};
    nds::AicpuRdmaPostLauncher launcher;

    const bool provider_probe = argc == 6 && std::string(argv[5]) == "--provider";
    if (argc != 5 && !provider_probe) {
        std::cerr << "usage: " << argv[0] << " --ascendcl ABS_PATH --aicpu-kernel-config ABS_PATH [--provider]\n";
        return EXIT_FAILURE;
    }
    const std::string ascendcl = argv[2];
    const std::string config = argv[4];
    if (std::string(argv[1]) != "--ascendcl" || std::string(argv[3]) != "--aicpu-kernel-config" ||
        nds_acl_open(&acl, ascendcl.c_str()) != 0) {
        std::cerr << "AscendCL loader setup failed: " << nds_acl_error(&acl) << '\n';
        return EXIT_FAILURE;
    }
    bool initialized = false;
    bool ok = false;
    bool probe_ok = false;
    if (acl.init(nullptr) != 0 || acl.set_device(0) != 0) {
        std::cerr << "AscendCL initialization/device selection failed\n";
        goto out;
    }
    initialized = true;
    if (!launcher.load(acl, config)) {
        std::cerr << "NDS AICPU " << (provider_probe ? "provider" : "no-op")
                  << " probe failed: " << launcher.error() << '\n';
        goto out;
    }
    probe_ok = provider_probe ? launcher.launch_provider_probe_and_wait(5000)
                              : launcher.launch_noop_and_wait(5000);
    if (!probe_ok) {
        std::cerr << "NDS AICPU " << (provider_probe ? "provider" : "no-op")
                  << " probe failed: " << launcher.error() << '\n';
        goto out;
    }
    std::cout << "NDS AICPU " << (provider_probe ? "provider" : "no-op") << " probe completed.\n";
    ok = true;
out:
    launcher.reset();
    if (initialized) (void)acl.finalize();
    nds_acl_close(&acl);
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}

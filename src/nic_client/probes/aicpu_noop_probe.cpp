#include "nds/acl_loader.h"
#include "nds/aicpu_roce.hpp"
#include "nds/npu_ra_context.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

int main(int argc, char **argv)
{
    nds_acl_api acl{};
    nds::AicpuRdmaPostLauncher launcher;

    bool provider_probe = false;
    std::int32_t cpu_kernel_mode = 1;
    std::string runtime;
    std::string ra;
    if (argc == 6 && std::string(argv[5]) == "--provider") provider_probe = true;
    else if ((argc == 11 || argc == 12) && std::string(argv[5]) == "--cpu-kernel-mode" && std::string(argv[6]) == "0" &&
             std::string(argv[7]) == "--runtime" && std::string(argv[9]) == "--ra") {
        cpu_kernel_mode = 0;
        runtime = argv[8];
        ra = argv[10];
        if (argc == 12 && std::string(argv[11]) == "--provider") provider_probe = true;
        else if (argc == 12) {
            std::cerr << "invalid mode-0 AICPU probe option\n";
            return EXIT_FAILURE;
        }
    }
    else if (argc != 5) {
        std::cerr << "usage: " << argv[0]
                  << " --ascendcl ABS_PATH --aicpu-kernel-config ABS_PATH"
                  << " [--provider|--cpu-kernel-mode 0 --runtime ABS_PATH --ra ABS_PATH [--provider]]\n";
        return EXIT_FAILURE;
    }
    const std::string ascendcl = argv[2];
    const std::string config = argv[4];
    nds::NpuRaContext direct_context;
    if (std::string(argv[1]) != "--ascendcl" || std::string(argv[3]) != "--aicpu-kernel-config") {
        std::cerr << "invalid AICPU probe arguments\n";
        return EXIT_FAILURE;
    }
    const bool direct_context_owned = cpu_kernel_mode == 0;
    if (!direct_context_owned && nds_acl_open(&acl, ascendcl.c_str()) != 0) {
        std::cerr << "AscendCL loader setup failed: " << nds_acl_error(&acl) << '\n';
        return EXIT_FAILURE;
    }
    bool initialized = false;
    bool ok = false;
    bool probe_ok = false;
    if (direct_context_owned) {
        const nds::NpuRaContextConfig context_config{ascendcl, runtime, ra, 0U, 0U};
        if (!direct_context.initialize(context_config)) {
            std::cerr << "direct NPU context setup failed: " << direct_context.error() << '\n';
            goto out;
        }
        acl = direct_context.acl_api();
    } else {
        if (acl.init(nullptr) != 0 || acl.set_device(0) != 0) {
            std::cerr << "AscendCL initialization/device selection failed\n";
            goto out;
        }
        initialized = true;
    }
    if (!launcher.load(acl, config, cpu_kernel_mode)) {
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
    direct_context.reset();
    if (initialized) (void)acl.finalize();
    if (!direct_context_owned) nds_acl_close(&acl);
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}

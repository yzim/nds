#include "nds/cann_context.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>


int main()
{
    nds::CannContext context;
    nds::CannContextConfig invalid{};

    if (context.initialize(invalid) || context.initialized() || context.error().empty()) {
        std::cerr << "invalid context configuration was not rejected\n";
        return 1;
    }
    invalid.ascendcl_library = "relative/libascendcl.so";
    invalid.tsdclient_library = "/definitely/not/a/cann/libtsdclient.so";
    invalid.hcomm_library = "/definitely/not/a/cann/libhcomm.so";
    invalid.rank_table_path = "/unused/rank_table.json";
    if (context.initialize(invalid) || context.initialized() ||
        context.error().find("AscendCL library path must be absolute") == std::string::npos) {
        std::cerr << "relative CANN library path was not rejected: " << context.error() << '\n';
        return 1;
    }

    const std::filesystem::path temporary_root = std::filesystem::temp_directory_path() / "nds-cann-context-test";
    const std::filesystem::path first_directory = temporary_root / "first";
    const std::filesystem::path second_directory = temporary_root / "second";
    std::error_code cleanup_error;
    std::filesystem::remove_all(temporary_root, cleanup_error);
    std::filesystem::create_directories(first_directory);
    std::filesystem::create_directories(second_directory);
    for (const auto &path : {first_directory / "libascendcl.so", first_directory / "libtsdclient.so",
                             second_directory / "libhcomm.so"}) {
        std::ofstream(path).put('x');
    }
    invalid.ascendcl_library = (first_directory / "libascendcl.so").string();
    invalid.tsdclient_library = (first_directory / "libtsdclient.so").string();
    invalid.hcomm_library = (second_directory / "libhcomm.so").string();
    if (context.initialize(invalid) || context.initialized() ||
        context.error().find("CANN libraries must come from one directory") == std::string::npos) {
        std::cerr << "mixed CANN library directories were not rejected: " << context.error() << '\n';
        std::filesystem::remove_all(temporary_root, cleanup_error);
        return 1;
    }
    std::filesystem::remove_all(temporary_root, cleanup_error);
    context.reset();
    if (context.initialized()) {
        std::cerr << "reset did not clear failed context state\n";
        return 1;
    }
    std::cout << "CANN context preflight tests passed\n";
    return 0;
}

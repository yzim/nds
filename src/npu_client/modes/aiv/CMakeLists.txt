add_library(nds_aiv_roce STATIC launcher.cpp)
target_include_directories(nds_aiv_roce PUBLIC include)
target_link_libraries(nds_aiv_roce PUBLIC nds_ra_loader)
target_compile_options(nds_aiv_roce PRIVATE -Wall -Wextra -Wpedantic -Werror)

if(NDS_BUILD_AIV_KERNEL)
    add_subdirectory(kernel)
endif()

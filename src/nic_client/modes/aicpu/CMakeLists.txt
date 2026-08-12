add_library(nds_aicpu_roce STATIC launcher.cpp)
target_include_directories(nds_aicpu_roce PUBLIC include)
target_link_libraries(nds_aicpu_roce PUBLIC nds_ra_loader)
target_compile_options(nds_aicpu_roce PRIVATE -Wall -Wextra -Wpedantic -Werror)

if(NDS_BUILD_AICPU_KERNEL)
    add_subdirectory(device)
endif()

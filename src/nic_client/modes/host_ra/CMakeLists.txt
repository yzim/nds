add_library(nds_host_ra STATIC submitter.cpp)
target_include_directories(nds_host_ra PUBLIC include)
target_link_libraries(nds_host_ra PUBLIC nds_npu_ra_context nds_npu_ra_qp)
target_compile_options(nds_host_ra PRIVATE -Wall -Wextra -Wpedantic -Werror)

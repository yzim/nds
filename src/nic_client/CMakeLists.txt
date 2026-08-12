add_subdirectory(loaders)

add_library(nds_npu_ra_context STATIC core/npu_ra_context.cpp)
target_include_directories(nds_npu_ra_context PUBLIC include)
target_link_libraries(nds_npu_ra_context PUBLIC nds_ra_loader)
target_compile_options(nds_npu_ra_context PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(nds_npu_ra_qp STATIC core/npu_ra_qp.cpp)
target_include_directories(nds_npu_ra_qp PUBLIC include)
target_link_libraries(nds_npu_ra_qp PUBLIC nds_ra_loader nds_wire)
target_compile_options(nds_npu_ra_qp PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_subdirectory(modes/host_ra)
add_subdirectory(modes/aiv)
add_subdirectory(modes/aicpu)
add_subdirectory(probes)

add_executable(nds_npu_qp_client client.cpp)
target_link_libraries(nds_npu_qp_client PRIVATE
    nds_npu_ra_context
    nds_npu_ra_qp
    nds_control_plane
    nds_host_ra
    nds_aiv_roce
    nds_aicpu_roce)
target_compile_options(nds_npu_qp_client PRIVATE -Wall -Wextra -Wpedantic -Werror)

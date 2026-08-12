add_executable(nds_ra_probe ra_probe.c)
target_link_libraries(nds_ra_probe PRIVATE nds_ra_loader)
target_compile_options(nds_ra_probe PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(nds_ra_init_probe ra_init_probe.c)
target_link_libraries(nds_ra_init_probe PRIVATE nds_ra_loader)
target_compile_options(nds_ra_init_probe PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(nds_ra_rdev_probe ra_rdev_probe.c)
target_link_libraries(nds_ra_rdev_probe PRIVATE nds_ra_loader)
target_compile_options(nds_ra_rdev_probe PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(nds_tsd_probe tsd_probe.c)
target_link_libraries(nds_tsd_probe PRIVATE nds_ra_loader)
target_compile_options(nds_tsd_probe PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(nds_npu_ra_mr_probe npu_ra_mr_probe.cpp)
target_link_libraries(nds_npu_ra_mr_probe PRIVATE nds_npu_ra_context nds_npu_ra_qp)
target_compile_options(nds_npu_ra_mr_probe PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(nds_aicpu_noop_probe aicpu_noop_probe.cpp)
target_link_libraries(nds_aicpu_noop_probe PRIVATE nds_aicpu_roce nds_npu_ra_context nds_ra_loader)
target_compile_options(nds_aicpu_noop_probe PRIVATE -Wall -Wextra -Wpedantic -Werror)

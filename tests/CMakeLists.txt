add_executable(nds_rdma_wire_test rdma_wire_test.cpp)
target_link_libraries(nds_rdma_wire_test PRIVATE nds_wire)
target_compile_options(nds_rdma_wire_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME nds_rdma_wire_test COMMAND nds_rdma_wire_test)

add_executable(nds_rdma_path_mtu_test rdma_path_mtu_test.cpp)
target_link_libraries(nds_rdma_path_mtu_test PRIVATE nds_rdma_path_mtu)
target_compile_options(nds_rdma_path_mtu_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME nds_rdma_path_mtu_test COMMAND nds_rdma_path_mtu_test)

add_library(nds_fake_runtime SHARED fake_runtime.cpp)
target_compile_options(nds_fake_runtime PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(nds_runtime_loader_test runtime_loader_test.cpp)
target_link_libraries(nds_runtime_loader_test PRIVATE nds_ra_loader)
target_compile_options(nds_runtime_loader_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME nds_runtime_loader_test COMMAND nds_runtime_loader_test $<TARGET_FILE:nds_fake_runtime>)

add_executable(nds_control_plane_test control_plane_test.cpp)
target_link_libraries(nds_control_plane_test PRIVATE nds_control_plane)
target_compile_options(nds_control_plane_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME nds_control_plane_test COMMAND nds_control_plane_test)

add_executable(nds_npu_ra_qp_test npu_ra_qp_test.cpp)
target_link_libraries(nds_npu_ra_qp_test PRIVATE nds_npu_ra_qp)
target_compile_options(nds_npu_ra_qp_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME nds_npu_ra_qp_test COMMAND nds_npu_ra_qp_test)

add_executable(nds_aicpu_roce_abi_test aicpu_roce_abi_test.cpp)
target_link_libraries(nds_aicpu_roce_abi_test PRIVATE nds_aicpu_roce)
target_compile_options(nds_aicpu_roce_abi_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME nds_aicpu_roce_abi_test COMMAND nds_aicpu_roce_abi_test)

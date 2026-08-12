function(nds_add_unit_test target)
    add_executable(${target} ${ARGN})
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME unit.${target} COMMAND ${target})
    set_tests_properties(unit.${target} PROPERTIES LABELS "unit")
endfunction()

nds_add_unit_test(nds_rdma_wire_test rdma_wire_test.cpp)
target_link_libraries(nds_rdma_wire_test PRIVATE nds_wire)

nds_add_unit_test(nds_rdma_path_mtu_test rdma_path_mtu_test.cpp)
target_link_libraries(nds_rdma_path_mtu_test PRIVATE nds_rdma_path_mtu)

nds_add_unit_test(nds_npu_ra_qp_test npu_ra_qp_test.cpp)
target_link_libraries(nds_npu_ra_qp_test PRIVATE nds_npu_ra_qp)

nds_add_unit_test(nds_aicpu_roce_abi_test aicpu_roce_abi_test.cpp)
target_link_libraries(nds_aicpu_roce_abi_test PRIVATE nds_aicpu_roce)

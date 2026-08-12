option(NDS_ENABLE_E2E_TESTS "Register hardware end-to-end tests that require an Ascend NPU and CPU RNIC" OFF)

if(NDS_ENABLE_E2E_TESTS)
    if(NOT TARGET nds_npu_qp_client OR NOT TARGET nds_verbs_server)
        message(FATAL_ERROR "NDS E2E tests require both nds_npu_qp_client and nds_verbs_server")
    endif()
    if(NOT DEFINED NDS_E2E_COMMAND OR NDS_E2E_COMMAND STREQUAL "")
        message(FATAL_ERROR "NDS_E2E_COMMAND must run one bounded real-NPU end-to-end case")
    endif()

    add_test(NAME e2e.npu_to_cpu_write COMMAND ${NDS_E2E_COMMAND})
    set_tests_properties(e2e.npu_to_cpu_write PROPERTIES
        LABELS "e2e;requires-ascend-npu;requires-rdma"
        TIMEOUT 120)
endif()

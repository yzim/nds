if(NOT TARGET nds_client OR NOT TARGET nds_server)
    message(FATAL_ERROR "NDS E2E tests require both nds_client and nds_server")
endif()

function(nds_add_storage_e2e mode)
    add_test(NAME e2e.${mode}_storage
        COMMAND ${CMAKE_COMMAND} -E env NDS_E2E_MODE=${mode}
            ${CMAKE_CURRENT_SOURCE_DIR}/run_storage.sh)
    set_tests_properties(e2e.${mode}_storage PROPERTIES
        LABELS "e2e;requires-ascend-npu;requires-rdma;mode-${mode};operation-storage"
        TIMEOUT 120
        RESOURCE_LOCK ascend_npu0)
endfunction()

nds_add_storage_e2e(ra)
if(TARGET nds_aiv_kernel)
    nds_add_storage_e2e(aiv)
endif()
if(TARGET nds_aicpu_kernel)
    nds_add_storage_e2e(aicpu)
endif()

if(NOT TARGET nds_client OR NOT TARGET nds_server)
    message(FATAL_ERROR "NDS E2E tests require both nds_client and nds_server")
endif()

function(nds_add_storage_e2e backend operation)
    add_test(NAME e2e.${backend}_${operation}_storage
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run_storage.sh
            --backend ${backend} --operation ${operation})
    set_tests_properties(e2e.${backend}_${operation}_storage PROPERTIES
        LABELS "e2e;requires-ascend-npu;requires-rdma;mode-${backend};operation-${operation}"
        TIMEOUT 120
        RESOURCE_LOCK ascend_npu0)
endfunction()

foreach(operation read write)
    nds_add_storage_e2e(ra ${operation})
endforeach()
if(TARGET nds_aiv_kernel)
    foreach(operation read write)
        nds_add_storage_e2e(aiv ${operation})
    endforeach()
endif()
if(TARGET nds_aicpu_kernel)
    foreach(operation read write)
        nds_add_storage_e2e(aicpu ${operation})
    endforeach()
endif()

if(TARGET _nds_torch)
    add_test(NAME e2e.torch_storage_session
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run_torch_storage.sh)
    set_tests_properties(e2e.torch_storage_session PROPERTIES
        LABELS "e2e;requires-ascend-npu;requires-rdma;torch;mode-ra"
        TIMEOUT 120
        RESOURCE_LOCK ascend_npu0
        ENVIRONMENT "NDS_E2E_SOURCE_DIR=${PROJECT_SOURCE_DIR}")
endif()

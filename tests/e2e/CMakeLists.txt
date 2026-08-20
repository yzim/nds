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

foreach(operation read write batch-read batch-write)
    nds_add_storage_e2e(ra ${operation})
endforeach()
if(TARGET nds_aiv_kernel)
    foreach(operation read write batch-read batch-write)
        nds_add_storage_e2e(aiv ${operation})
    endforeach()
endif()
if(TARGET nds_aicpu_kernel)
    foreach(operation read write batch-read batch-write)
        nds_add_storage_e2e(aicpu ${operation})
    endforeach()
endif()

function(nds_add_example_e2e layer backend)
    add_test(NAME e2e.${backend}_${layer}_example
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run_example.sh --layer ${layer} --backend ${backend})
    set_tests_properties(e2e.${backend}_${layer}_example PROPERTIES
        LABELS "e2e;requires-ascend-npu;requires-rdma;example-${layer};mode-${backend}"
        TIMEOUT 120
        RESOURCE_LOCK ascend_npu0)
endfunction()

foreach(layer verbs transport)
    if(TARGET nds_${layer}_client AND TARGET nds_${layer}_server)
        nds_add_example_e2e(${layer} ra)
        if(TARGET nds_aiv_kernel)
            nds_add_example_e2e(${layer} aiv)
        endif()
        if(TARGET nds_aicpu_kernel)
            nds_add_example_e2e(${layer} aicpu)
        endif()
    endif()
endforeach()

if(TARGET _nds_torch)
    function(nds_add_torch_storage_e2e backend)
        add_test(NAME e2e.${backend}_torch_storage_session
            COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run_torch_storage.sh --backend ${backend})
        set_tests_properties(e2e.${backend}_torch_storage_session PROPERTIES
            LABELS "e2e;requires-ascend-npu;requires-rdma;torch;mode-${backend}"
            TIMEOUT 120
            RESOURCE_LOCK ascend_npu0
            ENVIRONMENT "NDS_E2E_SOURCE_DIR=${PROJECT_SOURCE_DIR}")
    endfunction()

    nds_add_torch_storage_e2e(ra)
    if(TARGET nds_aiv_kernel)
        nds_add_torch_storage_e2e(aiv)
    endif()
    if(TARGET nds_aicpu_kernel)
        nds_add_torch_storage_e2e(aicpu)
    endif()
endif()

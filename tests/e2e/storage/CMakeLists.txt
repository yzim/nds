if(NOT TARGET nds_storage_client OR NOT TARGET nds_storage_server)
    message(FATAL_ERROR "NDS storage E2E tests require both storage example targets")
endif()

function(nds_add_storage_e2e backend operation)
    add_test(NAME e2e.storage.${backend}.${operation}
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run.sh
            --backend-mode ${backend} --operation ${operation})
    set_tests_properties(e2e.storage.${backend}.${operation} PROPERTIES
        LABELS "e2e;requires-ascend-npu;requires-rdma;layer-storage;mode-${backend};operation-${operation}"
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

if(TARGET _nds_torch)
    function(nds_add_torch_storage_e2e backend)
        add_test(NAME e2e.storage.${backend}.torch-session
            COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run_torch.sh --backend-mode ${backend})
        set_tests_properties(e2e.storage.${backend}.torch-session PROPERTIES
            LABELS "e2e;requires-ascend-npu;requires-rdma;layer-storage;torch;mode-${backend}"
            TIMEOUT 120
            RESOURCE_LOCK ascend_npu0
            ENVIRONMENT "NDS_E2E_SOURCE_DIR=${PROJECT_SOURCE_DIR};NDS_E2E_TORCH_PYTHON=${Python3_EXECUTABLE}")
    endfunction()

    nds_add_torch_storage_e2e(ra)
    if(TARGET nds_aiv_kernel)
        nds_add_torch_storage_e2e(aiv)
    endif()
    if(TARGET nds_aicpu_kernel)
        nds_add_torch_storage_e2e(aicpu)
    endif()
endif()

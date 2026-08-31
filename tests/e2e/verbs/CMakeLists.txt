if(TARGET nds_verbs_client AND TARGET nds_verbs_server)
    function(nds_add_verbs_e2e backend operation)
        add_test(NAME e2e.verbs.${backend}.${operation}
            COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run.sh
                --backend-mode ${backend} --operation ${operation})
        set_tests_properties(e2e.verbs.${backend}.${operation} PROPERTIES
            LABELS "e2e;requires-ascend-npu;requires-rdma;layer-verbs;mode-${backend};operation-${operation}"
            TIMEOUT 120
            RESOURCE_LOCK ascend_npu0)
    endfunction()

    nds_add_verbs_e2e(ra send)
    nds_add_verbs_e2e(ra recv)
    if(TARGET nds_aiv_kernel)
        nds_add_verbs_e2e(aiv send)
        nds_add_verbs_e2e(aiv recv)
        nds_add_verbs_e2e(aiv send-batch)
    endif()
    if(TARGET nds_aicpu_kernel)
        nds_add_verbs_e2e(aicpu send)
        nds_add_verbs_e2e(aicpu recv)
    endif()
endif()

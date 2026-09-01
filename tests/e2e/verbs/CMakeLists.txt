if(TARGET nds_verbs_client AND TARGET nds_verbs_server)
    function(nds_add_verbs_e2e backend)
        add_test(NAME e2e.verbs.${backend}
            COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run.sh
                --backend-mode ${backend})
        set_tests_properties(e2e.verbs.${backend} PROPERTIES
            LABELS "e2e;requires-ascend-npu;requires-rdma;layer-verbs;mode-${backend};operation-send-recv-write"
            TIMEOUT 120
            RESOURCE_LOCK ascend_npu0)
    endfunction()

    nds_add_verbs_e2e(ra)
    if(TARGET nds_aiv_kernel)
        nds_add_verbs_e2e(aiv)
    endif()
    if(TARGET nds_aicpu_kernel)
        nds_add_verbs_e2e(aicpu)
    endif()
endif()

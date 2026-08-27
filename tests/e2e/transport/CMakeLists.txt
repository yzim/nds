if(TARGET nds_transport_client AND TARGET nds_transport_server)
    function(nds_add_transport_e2e backend operation)
        add_test(NAME e2e.transport.${backend}.${operation}
            COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run.sh
                --backend ${backend} --operation ${operation})
        set_tests_properties(e2e.transport.${backend}.${operation} PROPERTIES
            LABELS "e2e;requires-ascend-npu;requires-rdma;layer-transport;mode-${backend};operation-${operation}"
            TIMEOUT 120
            RESOURCE_LOCK ascend_npu0)
    endfunction()

    foreach(operation send recv read write)
        nds_add_transport_e2e(ra ${operation})
    endforeach()
    if(TARGET nds_aiv_kernel)
        foreach(operation send recv read write)
            nds_add_transport_e2e(aiv ${operation})
        endforeach()
    endif()
    if(TARGET nds_aicpu_kernel)
        foreach(operation send recv read write)
            nds_add_transport_e2e(aicpu ${operation})
        endforeach()
    endif()
endif()

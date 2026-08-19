add_subdirectory(nds_client)
add_subdirectory(nds_server)

if(NDS_BUILD_HARDWARE_PROBES)
    add_subdirectory(${PROJECT_SOURCE_DIR}/examples/layer_probe
                     ${CMAKE_CURRENT_BINARY_DIR}/layer_probe)
endif()

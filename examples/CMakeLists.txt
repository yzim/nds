add_subdirectory(storage)

if(NDS_BUILD_HARDWARE_PROBES AND NDS_BUILD_NPU_CLIENT)
    add_subdirectory(verbs)
    add_subdirectory(transport)
endif()

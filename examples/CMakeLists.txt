add_subdirectory(storage)

if((NDS_BUILD_HARDWARE_PROBES OR NDS_BUILD_BENCHMARKS) AND NDS_BUILD_NPU_CLIENT)
    add_subdirectory(verbs)
endif()

if(NDS_BUILD_HARDWARE_PROBES AND NDS_BUILD_NPU_CLIENT)
    add_subdirectory(transport)
endif()

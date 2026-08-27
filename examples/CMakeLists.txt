add_subdirectory(storage)

if(NDS_BUILD_HARDWARE_PROBES)
    add_subdirectory(verbs)
    add_subdirectory(transport)
endif()

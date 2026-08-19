execute_process(
    COMMAND "${CMAKE_COMMAND}" -E tar tf "${NDS_AICPU_PACKAGE}"
    RESULT_VARIABLE list_result
    OUTPUT_VARIABLE package_entries)
if(NOT list_result EQUAL 0)
    message(FATAL_ERROR "cannot inspect NDS standard AICPU package")
endif()
if(NOT package_entries MATCHES "aicpu_kernels_device/libnds_aicpu_standard.so")
    message(FATAL_ERROR "standard AICPU package is missing libnds_aicpu_standard.so")
endif()
if(NOT package_entries MATCHES "aicpu_kernels_device/bin_hash.cfg")
    message(FATAL_ERROR "standard AICPU package is missing bin_hash.cfg")
endif()

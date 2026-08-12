if(NOT INPUT_SO OR NOT STAGE_ROOT OR NOT OUTPUT_PACKAGE)
    message(FATAL_ERROR "standard AICPU package requires INPUT_SO, STAGE_ROOT, and OUTPUT_PACKAGE")
endif()

get_filename_component(kernel_name "${INPUT_SO}" NAME)
set(package_root "${STAGE_ROOT}/aicpu_kernels_device")
file(REMOVE_RECURSE "${STAGE_ROOT}")
file(MAKE_DIRECTORY "${package_root}")
file(COPY "${INPUT_SO}" DESTINATION "${package_root}")
file(CHMOD "${package_root}/${kernel_name}"
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE)
file(SHA256 "${INPUT_SO}" kernel_sha256)
file(WRITE "${package_root}/bin_hash.cfg" "${kernel_name}=${kernel_sha256}\n")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E tar czf "${OUTPUT_PACKAGE}" aicpu_kernels_device
    WORKING_DIRECTORY "${STAGE_ROOT}"
    RESULT_VARIABLE package_result)
if(NOT package_result EQUAL 0)
    message(FATAL_ERROR "failed to create standard AICPU package: ${package_result}")
endif()

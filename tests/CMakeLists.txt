function(nds_add_unit_test target)
    add_executable(${target} unit/${ARGN})
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME unit.${target} COMMAND ${target})
    set_tests_properties(unit.${target} PROPERTIES LABELS "unit")
endfunction()

nds_add_unit_test(nds_transport_test transport_test.cc)
target_link_libraries(nds_transport_test PRIVATE nds_transport nds_protocol)
nds_add_unit_test(nds_transport_mtu_test transport_mtu_test.cc)
target_link_libraries(nds_transport_mtu_test PRIVATE nds_transport)
nds_add_unit_test(nds_npu_ra_qp_test npu_ra_qp_test.cc)
target_link_libraries(nds_npu_ra_qp_test PRIVATE nds_npu_ra_qp nds_ra)
nds_add_unit_test(nds_device_hns_codec_test device_hns_codec_test.cc)
target_include_directories(nds_device_hns_codec_test PRIVATE
    ${PROJECT_SOURCE_DIR}/src/client/include)
nds_add_unit_test(nds_device_connection_test device_connection_test.cc)
target_include_directories(nds_device_connection_test PRIVATE
    ${PROJECT_SOURCE_DIR}/src/client/include)
nds_add_unit_test(nds_device_storage_test device_storage_test.cc)
target_include_directories(nds_device_storage_test PRIVATE
    ${PROJECT_SOURCE_DIR}/src/client/include
    ${PROJECT_SOURCE_DIR}/src/include)
nds_add_unit_test(nds_logging_test logging_test.cc)
target_link_libraries(nds_logging_test PRIVATE nds_logging)

add_library(nds_fake_runtime SHARED integration/fake_runtime.cc)
target_compile_options(nds_fake_runtime PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_executable(nds_runtime_loader_test integration/runtime_loader_test.cc)
target_link_libraries(nds_runtime_loader_test PRIVATE nds_ra_loader)
target_compile_options(nds_runtime_loader_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME integration.nds_runtime_loader_test COMMAND nds_runtime_loader_test $<TARGET_FILE:nds_fake_runtime>)
set_tests_properties(integration.nds_runtime_loader_test PROPERTIES LABELS "integration")
add_executable(nds_transport_exchange_test integration/transport_exchange_test.cc)
target_link_libraries(nds_transport_exchange_test PRIVATE nds_transport)
target_compile_options(nds_transport_exchange_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME integration.nds_transport_exchange_test COMMAND nds_transport_exchange_test)
set_tests_properties(integration.nds_transport_exchange_test PROPERTIES LABELS "integration")

if(TARGET nds_aiv_kernel)
    add_test(NAME toolchain.aiv_device_api_symbols
        COMMAND ${CMAKE_COMMAND}
            -DNDS_DEVICE_KIND=aiv
            -DNDS_ARTIFACT=${CMAKE_BINARY_DIR}/aiv/nds_aiv_kernel.o
            -DNDS_NM=${CMAKE_NM}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/integration/device_api_symbols.cmake)
    set_tests_properties(toolchain.aiv_device_api_symbols PROPERTIES LABELS "device-toolchain")
endif()
if(TARGET nds_aicpu_kernel)
    add_test(NAME toolchain.aicpu_device_api_symbols
        COMMAND ${CMAKE_COMMAND}
            -DNDS_DEVICE_KIND=aicpu
            -DNDS_ARTIFACT=${CMAKE_BINARY_DIR}/aicpu/libnds_aicpu_standard.so
            -DNDS_NM=${CMAKE_NM}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/integration/device_api_symbols.cmake)
    set_tests_properties(toolchain.aicpu_device_api_symbols PROPERTIES LABELS "device-toolchain")
endif()

option(NDS_ENABLE_E2E_TESTS "Register hardware end-to-end tests that require an Ascend NPU and CPU RNIC" OFF)
if(NDS_ENABLE_E2E_TESTS)
    if(NOT TARGET nds_client OR NOT TARGET nds_server)
        message(FATAL_ERROR "NDS E2E tests require both nds_client and nds_server")
    endif()
    function(nds_add_e2e_case name command_variable mode operation)
        if(DEFINED ${command_variable} AND NOT "${${command_variable}}" STREQUAL "")
            add_test(NAME e2e.${name} COMMAND ${${command_variable}})
            set_tests_properties(e2e.${name} PROPERTIES
                LABELS "e2e;requires-ascend-npu;requires-rdma;mode-${mode};operation-${operation}"
                TIMEOUT 120
                RESOURCE_LOCK ascend_npu0)
            set(NDS_E2E_CASE_REGISTERED TRUE PARENT_SCOPE)
        endif()
    endfunction()

    set(NDS_E2E_CASE_REGISTERED FALSE)
    nds_add_e2e_case(ra_storage_write NDS_E2E_RA_STORAGE_WRITE_COMMAND ra storage-write)
    nds_add_e2e_case(aiv_storage_write NDS_E2E_AIV_STORAGE_WRITE_COMMAND aiv storage-write)
    nds_add_e2e_case(aicpu_storage_write NDS_E2E_AICPU_STORAGE_WRITE_COMMAND aicpu storage-write)
    if(NOT NDS_E2E_CASE_REGISTERED)
        message(FATAL_ERROR "NDS_ENABLE_E2E_TESTS requires at least one named NDS_E2E_*_COMMAND")
    endif()
endif()

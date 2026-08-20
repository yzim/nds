include(GoogleTest)

function(nds_add_unit_test target)
    add_executable(${target} unit/${ARGN})
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_link_libraries(${target} PRIVATE GTest::gtest_main)
    gtest_discover_tests(${target} TEST_PREFIX "unit." PROPERTIES LABELS unit)
endfunction()

nds_add_unit_test(nds_transport_test transport_test.cc)
target_link_libraries(nds_transport_test PRIVATE nds_transport nds_protocol)
nds_add_unit_test(nds_transport_mtu_test transport_mtu_test.cc)
target_link_libraries(nds_transport_mtu_test PRIVATE nds_transport)
nds_add_unit_test(nds_endpoint_test endpoint_test.cc)
target_link_libraries(nds_endpoint_test PRIVATE nds_client_endpoint nds_ra)
nds_add_unit_test(nds_runtime_test runtime_test.cc)
target_link_libraries(nds_runtime_test PRIVATE nds_client_runtime)
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
nds_add_unit_test(nds_aicpu_launcher_test aicpu_launcher_test.cc)
target_link_libraries(nds_aicpu_launcher_test PRIVATE nds_aicpu_launcher)
nds_add_unit_test(nds_logging_test logging_test.cc)
target_link_libraries(nds_logging_test PRIVATE nds_logging)

add_test(NAME integration.nds_aicpu_manifest_test
    COMMAND ${CMAKE_COMMAND}
        -DNDS_AICPU_MANIFEST=${PROJECT_SOURCE_DIR}/src/client/execution/aicpu/device/package/nds_aicpu_standard.json.in
        -P ${CMAKE_CURRENT_SOURCE_DIR}/integration/aicpu_manifest_test.cmake)
set_tests_properties(integration.nds_aicpu_manifest_test PROPERTIES LABELS "integration")

add_library(nds_fake_runtime SHARED integration/fake_runtime.cc)
target_compile_options(nds_fake_runtime PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_executable(nds_runtime_loader_test integration/runtime_loader_test.cc)
target_link_libraries(nds_runtime_loader_test PRIVATE nds_ra_loader GTest::gtest_main)
target_compile_options(nds_runtime_loader_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_compile_definitions(nds_runtime_loader_test PRIVATE
    NDS_FAKE_RUNTIME_PATH="$<TARGET_FILE:nds_fake_runtime>")
add_dependencies(nds_runtime_loader_test nds_fake_runtime)
gtest_discover_tests(nds_runtime_loader_test TEST_PREFIX "integration." PROPERTIES LABELS integration)
add_executable(nds_transport_exchange_test integration/transport_exchange_test.cc)
target_link_libraries(nds_transport_exchange_test PRIVATE nds_transport GTest::gtest_main)
target_compile_options(nds_transport_exchange_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
gtest_discover_tests(nds_transport_exchange_test TEST_PREFIX "integration." PROPERTIES LABELS integration)

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
    add_test(NAME toolchain.aicpu_standard_package
        COMMAND ${CMAKE_COMMAND}
            -DNDS_AICPU_PACKAGE=${CMAKE_BINARY_DIR}/aicpu/aicpu_nds.tar.gz
            -P ${CMAKE_CURRENT_SOURCE_DIR}/integration/aicpu_package_test.cmake)
    set_tests_properties(toolchain.aicpu_standard_package PROPERTIES LABELS "device-toolchain")
endif()

option(NDS_ENABLE_E2E_TESTS "Register hardware end-to-end tests that require an Ascend NPU and CPU RNIC" OFF)
if(NDS_ENABLE_E2E_TESTS)
    add_subdirectory(e2e)
endif()

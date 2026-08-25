cmake_minimum_required(VERSION 3.20)

# Performance workloads are opt-in and target-specific. Keep this subtree
# independent from the correctness test suite and normal application build.
add_executable(nds_ra_verbs_bench_client ra_verbs_client.cc)
target_include_directories(nds_ra_verbs_bench_client PRIVATE
    ${PROJECT_SOURCE_DIR}/src/client/execution
    ${PROJECT_SOURCE_DIR}/src/client/resource)
target_link_libraries(nds_ra_verbs_bench_client PRIVATE
    CLI11::CLI11
    nds_logging
    nds_client_control)
target_compile_options(nds_ra_verbs_bench_client PRIVATE -Wall -Wextra -Wpedantic -Werror)
set_target_properties(nds_ra_verbs_bench_client PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_executable(nds_aicpu_verbs_bench_client aicpu_verbs_client.cc)
target_include_directories(nds_aicpu_verbs_bench_client PRIVATE
    ${PROJECT_SOURCE_DIR}/src/client/execution
    ${PROJECT_SOURCE_DIR}/src/client/execution/ra
    ${PROJECT_SOURCE_DIR}/src/client/resource)
target_link_libraries(nds_aicpu_verbs_bench_client PRIVATE
    CLI11::CLI11
    nds_logging
    nds_client_control
    nds_aicpu_launcher)
target_compile_options(nds_aicpu_verbs_bench_client PRIVATE -Wall -Wextra -Wpedantic -Werror)
set_target_properties(nds_aicpu_verbs_bench_client PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_executable(nds_aicpu_device_bench_client aicpu_device_bench_client.cc)
target_include_directories(nds_aicpu_device_bench_client PRIVATE
    ${PROJECT_SOURCE_DIR}/src/client/execution
    ${PROJECT_SOURCE_DIR}/src/client/resource
    ${PROJECT_SOURCE_DIR}/src/client/include)
target_link_libraries(nds_aicpu_device_bench_client PRIVATE
    CLI11::CLI11
    nds_logging
    nds_client_control
    nds_aicpu_launcher)
target_compile_options(nds_aicpu_device_bench_client PRIVATE -Wall -Wextra -Wpedantic -Werror)
set_target_properties(nds_aicpu_device_bench_client PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_executable(nds_aiv_verbs_bench_client aiv_verbs_client.cc)
target_include_directories(nds_aiv_verbs_bench_client PRIVATE
    ${PROJECT_SOURCE_DIR}/src/client/execution
    ${PROJECT_SOURCE_DIR}/src/client/resource)
target_link_libraries(nds_aiv_verbs_bench_client PRIVATE
    CLI11::CLI11
    nds_logging
    nds_client_control
    nds_aiv_launcher)
target_compile_options(nds_aiv_verbs_bench_client PRIVATE -Wall -Wextra -Wpedantic -Werror)
set_target_properties(nds_aiv_verbs_bench_client PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_executable(nds_aiv_device_bench_client aiv_device_bench_client.cc)
target_include_directories(nds_aiv_device_bench_client PRIVATE
    ${PROJECT_SOURCE_DIR}/src/client/execution
    ${PROJECT_SOURCE_DIR}/src/client/resource
    ${PROJECT_SOURCE_DIR}/src/client/include)
target_link_libraries(nds_aiv_device_bench_client PRIVATE
    CLI11::CLI11
    nds_logging
    nds_client_control
    nds_aiv_launcher)
target_compile_options(nds_aiv_device_bench_client PRIVATE -Wall -Wextra -Wpedantic -Werror)
set_target_properties(nds_aiv_device_bench_client PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_executable(nds_ra_verbs_bench_server ra_verbs_server.cc)
target_link_libraries(nds_ra_verbs_bench_server PRIVATE CLI11::CLI11 nds_logging nds_server_transport)
target_compile_options(nds_ra_verbs_bench_server PRIVATE -Wall -Wextra -Wpedantic -Werror)
set_target_properties(nds_ra_verbs_bench_server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_executable(nds_cpu_multi_verbs_bench_server cpu_multi_verbs_server.cc)
target_link_libraries(nds_cpu_multi_verbs_bench_server PRIVATE CLI11::CLI11 nds_logging nds_server_transport)
target_compile_options(nds_cpu_multi_verbs_bench_server PRIVATE -Wall -Wextra -Wpedantic -Werror)
set_target_properties(nds_cpu_multi_verbs_bench_server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_executable(nds_cpu_hbm_bench_server cpu_hbm_bench_server.cc)
target_link_libraries(nds_cpu_hbm_bench_server PRIVATE CLI11::CLI11 nds_logging nds_server_transport)
target_compile_options(nds_cpu_hbm_bench_server PRIVATE -Wall -Wextra -Wpedantic -Werror)
set_target_properties(nds_cpu_hbm_bench_server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_executable(nds_cpu_hbm_bench_peer cpu_hbm_bench_peer.cc)
target_include_directories(nds_cpu_hbm_bench_peer PRIVATE
    ${PROJECT_SOURCE_DIR}/src/client/execution
    ${PROJECT_SOURCE_DIR}/src/client/resource)
target_link_libraries(nds_cpu_hbm_bench_peer PRIVATE
    CLI11::CLI11
    nds_logging
    nds_client_control)
target_compile_options(nds_cpu_hbm_bench_peer PRIVATE -Wall -Wextra -Wpedantic -Werror)
set_target_properties(nds_cpu_hbm_bench_peer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_executable(nds_npu_peer_verbs_bench npu_peer_verbs_bench.cc)
target_include_directories(nds_npu_peer_verbs_bench PRIVATE
    ${PROJECT_SOURCE_DIR}/src/client/execution
    ${PROJECT_SOURCE_DIR}/src/client/execution/ra
    ${PROJECT_SOURCE_DIR}/src/client/resource)
target_link_libraries(nds_npu_peer_verbs_bench PRIVATE
    CLI11::CLI11
    nds_logging
    nds_client_control
    nds_aicpu_launcher
    nds_aiv_launcher)
target_compile_options(nds_npu_peer_verbs_bench PRIVATE -Wall -Wextra -Wpedantic -Werror)
set_target_properties(nds_npu_peer_verbs_bench PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

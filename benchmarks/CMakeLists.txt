cmake_minimum_required(VERSION 3.20)

# Performance workloads are opt-in and target-specific. Keep this subtree
# independent from the correctness test suite and normal application build.
if(NDS_BUILD_NPU_CLIENT AND TARGET nds_verbs_backend)
    add_executable(nds_verbs_benchmark_client verbs_client.cc)
    target_include_directories(nds_verbs_benchmark_client PRIVATE
        ${PROJECT_SOURCE_DIR}/src/client/backends
        ${PROJECT_SOURCE_DIR}/src/client)
    target_link_libraries(nds_verbs_benchmark_client PRIVATE
        CLI11::CLI11
        nds_logging
        nds_client_endpoint
        nds_client_runtime
        nds_backend_launchers
        nds_transport)
    target_compile_options(nds_verbs_benchmark_client PRIVATE -Wall -Wextra -Wpedantic -Werror)

    add_executable(nds_verbs_benchmark_server verbs_server.cc)
    target_include_directories(nds_verbs_benchmark_server PRIVATE
        ${PROJECT_SOURCE_DIR}/src/client/backends
        ${PROJECT_SOURCE_DIR}/src/client)
    target_link_libraries(nds_verbs_benchmark_server PRIVATE CLI11::CLI11 nds_logging nds_verbs_backend nds_transport)
    target_compile_options(nds_verbs_benchmark_server PRIVATE -Wall -Wextra -Wpedantic -Werror)
else()
    message(WARNING "verbs benchmark requires NDS_BUILD_NPU_CLIENT and libibverbs")
endif()

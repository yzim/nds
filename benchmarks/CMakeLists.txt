cmake_minimum_required(VERSION 3.20)

add_executable(nds_storage_bench_client storage_client.cc)
target_link_libraries(nds_storage_bench_client PRIVATE
    CLI11::CLI11
    nds_logging
    nds_client_storage)
target_compile_options(nds_storage_bench_client PRIVATE -Wall -Wextra -Wpedantic -Werror)
set_target_properties(nds_storage_bench_client PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

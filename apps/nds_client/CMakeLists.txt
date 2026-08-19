add_executable(nds_client main.cc)
target_link_libraries(nds_client PRIVATE
    CLI11::CLI11
    nds_logging
    nds_client_storage)
target_compile_options(nds_client PRIVATE -Wall -Wextra -Wpedantic -Werror)
set_target_properties(nds_client PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

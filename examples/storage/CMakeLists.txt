add_executable(nds_storage_client client.cc)
target_link_libraries(nds_storage_client PRIVATE
    CLI11::CLI11
    nds_logging
    nds_client_storage)
target_compile_options(nds_storage_client PRIVATE -Wall -Wextra -Wpedantic -Werror)
set_target_properties(nds_storage_client PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

if(TARGET nds_server_protocol)
    add_executable(nds_storage_server server.cc)
    target_link_libraries(nds_storage_server PRIVATE CLI11::CLI11 nds_logging nds_server_protocol)
    target_compile_options(nds_storage_server PRIVATE -Wall -Wextra -Wpedantic -Werror)
    set_target_properties(nds_storage_server PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()

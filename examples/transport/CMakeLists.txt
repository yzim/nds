add_executable(nds_transport_client client.cc)
target_include_directories(nds_transport_client PRIVATE
    ${PROJECT_SOURCE_DIR}/src/client/execution
    ${PROJECT_SOURCE_DIR}/src/client/resource)
target_link_libraries(nds_transport_client PRIVATE
    CLI11::CLI11
    nds_logging
    nds_client_control
    nds_aiv_launcher
    nds_aicpu_launcher)
target_compile_options(nds_transport_client PRIVATE -Wall -Wextra -Wpedantic -Werror)
set_target_properties(nds_transport_client PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

if(TARGET nds_server_transport)
    add_executable(nds_transport_server server.cc)
    target_link_libraries(nds_transport_server PRIVATE CLI11::CLI11 nds_logging nds_server_transport)
    target_compile_options(nds_transport_server PRIVATE -Wall -Wextra -Wpedantic -Werror)
    set_target_properties(nds_transport_server PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()

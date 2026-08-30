add_executable(nds_verbs_client client.cc)
target_include_directories(nds_verbs_client PRIVATE
    ${PROJECT_SOURCE_DIR}/src/client/backends
    ${PROJECT_SOURCE_DIR}/src/client)
target_link_libraries(nds_verbs_client PRIVATE
    CLI11::CLI11
    nds_logging
    nds_client_endpoint
    nds_client_runtime
    nds_backend_launchers
    nds_transport)
target_compile_options(nds_verbs_client PRIVATE -Wall -Wextra -Wpedantic -Werror)
set_target_properties(nds_verbs_client PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

if(TARGET nds_verbs_backend)
    add_executable(nds_verbs_server server.cc)
    target_include_directories(nds_verbs_server PRIVATE
        ${PROJECT_SOURCE_DIR}/src/client/backends
        ${PROJECT_SOURCE_DIR}/src/client)
    target_link_libraries(nds_verbs_server PRIVATE CLI11::CLI11 nds_logging nds_verbs_backend nds_transport)
    target_compile_options(nds_verbs_server PRIVATE -Wall -Wextra -Wpedantic -Werror)
    set_target_properties(nds_verbs_server PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()

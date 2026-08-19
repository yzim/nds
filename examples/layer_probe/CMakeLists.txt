add_executable(nds_layer_probe main.cc)
target_include_directories(nds_layer_probe PRIVATE
    ${PROJECT_SOURCE_DIR}/src/client/execution
    ${PROJECT_SOURCE_DIR}/src/client/resource)
target_link_libraries(nds_layer_probe PRIVATE
    CLI11::CLI11
    nds_logging
    nds_client_control
    nds_aiv_launcher
    nds_aicpu_launcher)
target_compile_options(nds_layer_probe PRIVATE -Wall -Wextra -Wpedantic -Werror)

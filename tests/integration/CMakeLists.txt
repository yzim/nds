add_library(nds_fake_runtime SHARED fake_runtime.cpp)
target_compile_options(nds_fake_runtime PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(nds_runtime_loader_test runtime_loader_test.cpp)
target_link_libraries(nds_runtime_loader_test PRIVATE nds_ra_loader)
target_compile_options(nds_runtime_loader_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME integration.nds_runtime_loader_test
         COMMAND nds_runtime_loader_test $<TARGET_FILE:nds_fake_runtime>)
set_tests_properties(integration.nds_runtime_loader_test PROPERTIES LABELS "integration")

add_executable(nds_control_plane_test control_plane_test.cpp)
target_link_libraries(nds_control_plane_test PRIVATE nds_control_plane)
target_compile_options(nds_control_plane_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME integration.nds_control_plane_test COMMAND nds_control_plane_test)
set_tests_properties(integration.nds_control_plane_test PROPERTIES LABELS "integration")

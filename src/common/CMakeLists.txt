find_package(Threads REQUIRED)

add_library(nds_wire STATIC rdma_wire.cpp)
target_include_directories(nds_wire PUBLIC include)
target_compile_options(nds_wire PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(nds_rdma_path_mtu STATIC rdma_path_mtu.cpp)
target_include_directories(nds_rdma_path_mtu PUBLIC include)
target_compile_options(nds_rdma_path_mtu PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(nds_control_plane STATIC control_plane.cpp)
target_include_directories(nds_control_plane PUBLIC include)
target_link_libraries(nds_control_plane PUBLIC nds_wire Threads::Threads)
target_compile_options(nds_control_plane PRIVATE -Wall -Wextra -Wpedantic -Werror)

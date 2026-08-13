find_package(Threads REQUIRED)

add_library(nds_wire STATIC rdma_wire.cc)
target_include_directories(nds_wire PUBLIC include)
target_compile_options(nds_wire PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(nds_rdma_path_mtu STATIC rdma_path_mtu.cc)
target_include_directories(nds_rdma_path_mtu PUBLIC include)
target_compile_options(nds_rdma_path_mtu PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(nds_peer_exchange STATIC peer_exchange.cc)
target_include_directories(nds_peer_exchange PUBLIC include)
target_link_libraries(nds_peer_exchange PUBLIC nds_wire Threads::Threads)
target_compile_options(nds_peer_exchange PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(nds_logging STATIC logging.cc)
target_include_directories(nds_logging PUBLIC include)
target_link_libraries(nds_logging PUBLIC spdlog::spdlog)
target_compile_options(nds_logging PRIVATE -Wall -Wextra -Wpedantic -Werror)

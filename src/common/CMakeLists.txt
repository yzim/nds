find_package(Threads REQUIRED)

add_library(nds_transport STATIC transport.cc)
target_include_directories(nds_transport PUBLIC include)
target_link_libraries(nds_transport PUBLIC Threads::Threads tl::expected)
target_compile_options(nds_transport PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(nds_protocol STATIC protocol.cc)
target_include_directories(nds_protocol PUBLIC include)
target_link_libraries(nds_protocol PUBLIC tl::expected)
target_compile_options(nds_protocol PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(nds_logging STATIC logging.cc)
target_include_directories(nds_logging PUBLIC include)
target_link_libraries(nds_logging PUBLIC spdlog::spdlog)
target_compile_options(nds_logging PRIVATE -Wall -Wextra -Wpedantic -Werror)

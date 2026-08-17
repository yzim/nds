find_package(Threads REQUIRED)

add_library(nds_connection STATIC connection.cc)
add_library(nds_transport ALIAS nds_connection)
target_include_directories(nds_connection PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_link_libraries(nds_connection PUBLIC Threads::Threads tl::expected)
target_compile_options(nds_connection PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(nds_protocol STATIC protocol.cc)
target_include_directories(nds_protocol PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_link_libraries(nds_protocol PUBLIC tl::expected)
target_compile_options(nds_protocol PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(nds_logging STATIC logging.cc)
target_include_directories(nds_logging PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_link_libraries(nds_logging PUBLIC spdlog::spdlog tl::expected)
target_compile_options(nds_logging PRIVATE -Wall -Wextra -Wpedantic -Werror)

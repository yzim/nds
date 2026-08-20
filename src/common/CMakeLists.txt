find_package(Threads REQUIRED)

add_library(nds_transport STATIC transport.cc)
target_include_directories(nds_transport PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_link_libraries(nds_transport PUBLIC Threads::Threads tl::expected)
target_compile_options(nds_transport PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(nds_storage_protocol INTERFACE)
target_include_directories(nds_storage_protocol INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/../include)

add_library(nds_logging STATIC logging.cc)
target_include_directories(nds_logging PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_link_libraries(nds_logging PUBLIC spdlog::spdlog tl::expected)
target_compile_options(nds_logging PRIVATE -Wall -Wextra -Wpedantic -Werror)

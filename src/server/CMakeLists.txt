find_package(PkgConfig REQUIRED)
pkg_check_modules(IBVERBS QUIET IMPORTED_TARGET libibverbs)

if(IBVERBS_FOUND)
    add_library(nds_verbs_backend STATIC backend.cc)
    target_include_directories(nds_verbs_backend PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(nds_verbs_backend PUBLIC nds_transport PkgConfig::IBVERBS)
    target_compile_options(nds_verbs_backend PRIVATE -Wall -Wextra -Wpedantic -Werror)

    add_library(nds_server_transport STATIC transport.cc)
    target_include_directories(nds_server_transport PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(nds_server_transport PUBLIC nds_verbs_backend nds_transport)
    target_compile_options(nds_server_transport PRIVATE -Wall -Wextra -Wpedantic -Werror)

    add_library(nds_server_protocol STATIC protocol.cc)
    target_include_directories(nds_server_protocol PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(nds_server_protocol PUBLIC nds_server_transport nds_storage_protocol)
    target_compile_options(nds_server_protocol PRIVATE -Wall -Wextra -Wpedantic -Werror)

else()
    message(WARNING "libibverbs was not found; nds_server will not be built")
endif()

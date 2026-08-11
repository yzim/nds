cmake_minimum_required(VERSION 3.20)
project(nds LANGUAGES C CXX)

include(CTest)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(IBVERBS QUIET IMPORTED_TARGET libibverbs)

option(NDS_LINK_PUBLIC_ACL "Link the public AscendCL API instead of resolving it at runtime" OFF)
set(NDS_CANN_ROOT "" CACHE PATH "CANN installation root, required when NDS_LINK_PUBLIC_ACL is enabled")

add_library(nds_wire STATIC src/common/rdma_wire.c)
target_include_directories(nds_wire PUBLIC include)
target_compile_options(nds_wire PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(nds_ra_loader STATIC
    src/npu_client/loaders/ra_loader.c
    src/npu_client/loaders/runtime_loader.c
    src/npu_client/loaders/acl_loader.c
    src/npu_client/loaders/tsd_loader.c
    src/npu_client/loaders/hcomm_loader.c
)
target_include_directories(nds_ra_loader PUBLIC include)
target_compile_options(nds_ra_loader PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_libraries(nds_ra_loader PUBLIC ${CMAKE_DL_LIBS})

if(NDS_LINK_PUBLIC_ACL)
    if(NOT NDS_CANN_ROOT)
        message(FATAL_ERROR "NDS_LINK_PUBLIC_ACL requires -DNDS_CANN_ROOT=<CANN root>")
    endif()
    set(NDS_CANN_LIBRARY_DIR "${NDS_CANN_ROOT}/lib64")
    find_path(NDS_ACL_INCLUDE_DIR NAMES acl/acl.h HINTS "${NDS_CANN_ROOT}/include" NO_DEFAULT_PATH REQUIRED)
    find_library(NDS_ASCENDCL_LIBRARY NAMES ascendcl HINTS "${NDS_CANN_LIBRARY_DIR}" NO_DEFAULT_PATH REQUIRED)
    target_compile_definitions(nds_ra_loader PRIVATE NDS_LINK_PUBLIC_ACL=1)
    target_include_directories(nds_ra_loader PRIVATE "${NDS_ACL_INCLUDE_DIR}")
    target_link_libraries(nds_ra_loader PUBLIC "${NDS_ASCENDCL_LIBRARY}")
    if(UNIX)
        # Pin executables that use this static adapter to the selected CANN root.
        # The CPU-only verbs target does not link nds_ra_loader, so it receives no CANN RPATH.
        target_link_options(nds_ra_loader INTERFACE "-Wl,-rpath,${NDS_CANN_LIBRARY_DIR}")
    endif()
endif()

add_executable(nds_ra_probe src/npu_client/probes/ra_probe.c)
target_link_libraries(nds_ra_probe PRIVATE nds_ra_loader)
target_compile_options(nds_ra_probe PRIVATE -Wall -Wextra -Wpedantic -Werror)

if(IBVERBS_FOUND)
    add_executable(nds_verbs_server src/cpu_server/verbs_server.c)
    target_include_directories(nds_verbs_server PRIVATE include)
    target_link_libraries(nds_verbs_server PRIVATE nds_wire PkgConfig::IBVERBS)
    target_compile_options(nds_verbs_server PRIVATE -Wall -Wextra -Wpedantic -Werror)
else()
    message(WARNING "libibverbs was not found; nds_verbs_server will not be built")
endif()

add_executable(nds_rdma_wire_test tests/rdma_wire_test.c)
target_link_libraries(nds_rdma_wire_test PRIVATE nds_wire)
target_compile_options(nds_rdma_wire_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME nds_rdma_wire_test COMMAND nds_rdma_wire_test)

add_executable(nds_ra_init_probe src/npu_client/probes/ra_init_probe.c)
target_link_libraries(nds_ra_init_probe PRIVATE nds_ra_loader)
target_compile_options(nds_ra_init_probe PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(nds_ra_rdev_probe src/npu_client/probes/ra_rdev_probe.c)
target_link_libraries(nds_ra_rdev_probe PRIVATE nds_ra_loader)
target_compile_options(nds_ra_rdev_probe PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(nds_tsd_probe src/npu_client/probes/tsd_probe.c)
target_link_libraries(nds_tsd_probe PRIVATE nds_ra_loader)
target_compile_options(nds_tsd_probe PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(nds_hcomm_context_probe src/npu_client/probes/hcomm_context_probe.c)
target_link_libraries(nds_hcomm_context_probe PRIVATE nds_ra_loader)
target_compile_options(nds_hcomm_context_probe PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(nds_hcomm_ra_rdev_probe src/npu_client/probes/hcomm_ra_rdev_probe.c)
target_link_libraries(nds_hcomm_ra_rdev_probe PRIVATE nds_ra_loader)
target_compile_options(nds_hcomm_ra_rdev_probe PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(nds_control_plane STATIC src/common/control_plane.cpp)
target_include_directories(nds_control_plane PUBLIC include)
target_link_libraries(nds_control_plane PUBLIC nds_wire Threads::Threads)
target_compile_options(nds_control_plane PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(nds_control_plane_test tests/control_plane_test.cpp)
target_link_libraries(nds_control_plane_test PRIVATE nds_control_plane)
target_compile_options(nds_control_plane_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME nds_control_plane_test COMMAND nds_control_plane_test)

add_library(nds_cann_context STATIC src/npu_client/cann_context.cpp)
target_include_directories(nds_cann_context PUBLIC include)
target_link_libraries(nds_cann_context PUBLIC nds_ra_loader)
target_compile_options(nds_cann_context PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(nds_npu_ra_context STATIC src/npu_client/npu_ra_context.cpp)
target_include_directories(nds_npu_ra_context PUBLIC include)
target_link_libraries(nds_npu_ra_context PUBLIC nds_ra_loader)
target_compile_options(nds_npu_ra_context PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(nds_npu_ra_qp STATIC src/npu_client/npu_ra_qp.cpp)
target_include_directories(nds_npu_ra_qp PUBLIC include)
target_link_libraries(nds_npu_ra_qp PUBLIC nds_ra_loader nds_wire)
target_compile_options(nds_npu_ra_qp PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(nds_npu_qp_client src/npu_client/npu_qp_client.cpp)
target_link_libraries(nds_npu_qp_client PRIVATE nds_npu_ra_context nds_control_plane nds_npu_ra_qp)
target_compile_options(nds_npu_qp_client PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(nds_npu_ra_qp_test tests/npu_ra_qp_test.cpp)
target_link_libraries(nds_npu_ra_qp_test PRIVATE nds_npu_ra_qp)
target_compile_options(nds_npu_ra_qp_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME nds_npu_ra_qp_test COMMAND nds_npu_ra_qp_test)

add_executable(nds_cann_context_test tests/cann_context_test.cpp)
target_link_libraries(nds_cann_context_test PRIVATE nds_cann_context)
target_compile_options(nds_cann_context_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME nds_cann_context_test COMMAND nds_cann_context_test)

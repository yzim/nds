add_library(nds_client_loaders STATIC
    loaders/libdsmi.cc
    loaders/libra.cc
    loaders/libruntime.cc
    loaders/shared_library.cc)
target_include_directories(nds_client_loaders
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/src/include
    PRIVATE loaders)
target_link_libraries(nds_client_loaders PUBLIC ${CMAKE_DL_LIBS} tl::expected)
target_compile_options(nds_client_loaders PRIVATE -Wall -Wextra -Wpedantic -Werror)
set_target_properties(nds_client_loaders PROPERTIES POSITION_INDEPENDENT_CODE ON)

find_path(NDS_ACL_INCLUDE_DIR NAMES acl/acl.h
    HINTS "${NDS_CANN_ROOT}/include" "${NDS_CANN_ROOT}/aarch64-linux/include" NO_DEFAULT_PATH REQUIRED)
find_library(NDS_ASCENDCL_LIBRARY NAMES ascendcl
    HINTS "${NDS_CANN_ROOT}/lib64" "${NDS_CANN_ROOT}/aarch64-linux/lib64" NO_DEFAULT_PATH REQUIRED)
get_filename_component(NDS_CANN_LIBRARY_DIR "${NDS_ASCENDCL_LIBRARY}" DIRECTORY)
get_filename_component(NDS_ASCEND_INSTALL_ROOT "${NDS_CANN_ROOT}" DIRECTORY)
find_library(NDS_ASCEND_HAL_LIBRARY NAMES ascend_hal
    HINTS "${NDS_ASCEND_INSTALL_ROOT}/driver/lib64/driver" REQUIRED)
add_library(nds_ascendcl INTERFACE)
target_include_directories(nds_ascendcl INTERFACE "${NDS_ACL_INCLUDE_DIR}")
target_link_libraries(nds_ascendcl INTERFACE "${NDS_ASCENDCL_LIBRARY}" "${NDS_ASCEND_HAL_LIBRARY}")
if(UNIX)
    get_filename_component(NDS_ASCEND_HAL_LIBRARY_DIR "${NDS_ASCEND_HAL_LIBRARY}" DIRECTORY)
    target_link_options(nds_ascendcl INTERFACE
        "-Wl,-rpath,${NDS_CANN_LIBRARY_DIR}"
        "-Wl,-rpath,${NDS_ASCEND_HAL_LIBRARY_DIR}")
endif()

add_library(nds_client_runtime STATIC runtime.cc)
target_include_directories(nds_client_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/src/include)
target_link_libraries(nds_client_runtime PUBLIC nds_client_loaders nds_ascendcl tl::expected)
target_compile_options(nds_client_runtime PRIVATE -Wall -Wextra -Wpedantic -Werror)
set_target_properties(nds_client_runtime PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(nds_client_endpoint STATIC endpoint.cc)
target_include_directories(nds_client_endpoint PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} include)
target_link_libraries(nds_client_endpoint PUBLIC nds_client_runtime nds_client_loaders nds_transport)
target_compile_options(nds_client_endpoint PRIVATE -Wall -Wextra -Wpedantic -Werror)
set_target_properties(nds_client_endpoint PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(nds_ra STATIC
    backends/ra/verbs.cc
    backends/ra/transport.cc
    backends/ra/storage.cc)
target_include_directories(nds_ra PUBLIC backends/ra ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(nds_ra PUBLIC nds_client_runtime nds_client_endpoint nds_logging)
target_compile_options(nds_ra PRIVATE -Wall -Wextra -Wpedantic -Werror)
set_target_properties(nds_ra PROPERTIES POSITION_INDEPENDENT_CODE ON)

# The NDS-owned RA package contains the verbs, transport, and storage backend layers.
add_library(nds_ra_backend SHARED
    backends/ra/entrypoints.cc
    backends/ra/verbs.cc
    backends/ra/transport.cc
    backends/ra/storage.cc)
target_include_directories(nds_ra_backend PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} backends/ra ${PROJECT_SOURCE_DIR}/src/include)
target_link_libraries(nds_ra_backend PRIVATE nds_client_runtime nds_client_endpoint nds_logging)
target_compile_options(nds_ra_backend PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(nds_backend_launchers STATIC
    backends/launcher.cc
    backends/ra/launcher.cc
    backends/aiv/launcher.cc
    backends/aicpu/launcher.cc)
target_include_directories(nds_backend_launchers PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} include ${PROJECT_SOURCE_DIR}/src/include)
target_link_libraries(nds_backend_launchers PUBLIC nds_ascendcl nds_client_loaders tl::expected)
target_compile_options(nds_backend_launchers PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(nds_client_control STATIC
    transport.cc)
target_include_directories(nds_client_control PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/backends)
target_link_libraries(nds_client_control PUBLIC
    nds_client_runtime
    nds_client_endpoint
    nds_transport)
target_compile_options(nds_client_control PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(nds_client_storage STATIC storage.cc)
target_include_directories(nds_client_storage PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/backends
    ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(nds_client_storage PUBLIC
    nds_client_control
    nds_ra
    nds_backend_launchers
    nds_storage_protocol)
target_compile_options(nds_client_storage PRIVATE -Wall -Wextra -Wpedantic -Werror)

if(NDS_BUILD_AIV_KERNEL)
    if(NOT CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
        message(FATAL_ERROR "NDS AIV kernel is only built on an aarch64 CANN host")
    endif()
    if(NOT NDS_CANN_ROOT)
        message(FATAL_ERROR "NDS_BUILD_AIV_KERNEL requires -DNDS_CANN_ROOT=<CANN root>")
    endif()

    set(NDS_CCEC "${NDS_CANN_ROOT}/aarch64-linux/bin/ccec")
    set(NDS_AIV_LD "${NDS_CANN_ROOT}/aarch64-linux/bin/ld.lld")
    if(NOT EXISTS "${NDS_CCEC}" OR NOT EXISTS "${NDS_AIV_LD}")
        message(FATAL_ERROR "CANN AIV CCEC/ld.lld tools were not found below ${NDS_CANN_ROOT}/aarch64-linux/bin")
    endif()

    set(NDS_AIV_DEVICE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/backends/aiv/device")
    set(NDS_AIV_OUTPUT_DIR "${CMAKE_BINARY_DIR}/aiv")
    set(NDS_AIV_OBJECT "${NDS_AIV_OUTPUT_DIR}/nds_aiv_kernel.o")
    set(NDS_AIV_ENTRY_OBJECT "${NDS_AIV_OUTPUT_DIR}/nds_aiv_entry.o")
    set(NDS_AIV_CCEC_INCLUDES
            -I${NDS_CANN_ROOT}/aarch64-linux/asc/include -I${NDS_CANN_ROOT}/aarch64-linux/asc
            -I${NDS_CANN_ROOT}/aarch64-linux/asc/include/interface -I${NDS_CANN_ROOT}/aarch64-linux/asc/include/basic_api
            -I${NDS_CANN_ROOT}/aarch64-linux/asc/include/micro_api -I${NDS_CANN_ROOT}/aarch64-linux/asc/impl
            -I${NDS_CANN_ROOT}/aarch64-linux/asc/impl/basic_api -I${NDS_CANN_ROOT}/aarch64-linux/asc/impl/micro_api
            -I${NDS_CANN_ROOT}/aarch64-linux/ascendc/include -I${NDS_CANN_ROOT}/aarch64-linux/ascendc/include/highlevel_api
            -I${NDS_AIV_DEVICE_DIR}
            -I${CMAKE_CURRENT_SOURCE_DIR}/backends/common
            -I${CMAKE_CURRENT_SOURCE_DIR}/include
            -I${PROJECT_SOURCE_DIR}/src/include)
    add_custom_command(
        OUTPUT "${NDS_AIV_OBJECT}"
        BYPRODUCTS "${NDS_AIV_ENTRY_OBJECT}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${NDS_AIV_OUTPUT_DIR}"
        COMMAND "${NDS_CCEC}" -c -x cce -O2 --cce-aicore-arch=dav-c220-vec --cce-aicore-only -std=c++17
            ${NDS_AIV_CCEC_INCLUDES}
            ${NDS_AIV_DEVICE_DIR}/entrypoints.cc -o ${NDS_AIV_ENTRY_OBJECT}
        COMMAND "${NDS_AIV_LD}" -m aicorelinux -Ttext=0 "${NDS_AIV_ENTRY_OBJECT}" -static -o "${NDS_AIV_OBJECT}"
        DEPENDS "${NDS_AIV_DEVICE_DIR}/entrypoints.cc"
            "${NDS_AIV_DEVICE_DIR}/verbs.cc"
            "${NDS_AIV_DEVICE_DIR}/transport.cc"
            "${NDS_AIV_DEVICE_DIR}/storage.cc"
            "${NDS_AIV_DEVICE_DIR}/api.h"
            "${NDS_AIV_DEVICE_DIR}/internal.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/include/device_transport.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/include/device_verbs.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/backends/common/hns_hw.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/include/device_storage.h"
            "${PROJECT_SOURCE_DIR}/src/include/storage_protocol.hh"
        VERBATIM)
    add_custom_target(nds_aiv_kernel ALL DEPENDS "${NDS_AIV_OBJECT}")
endif()

if(NDS_BUILD_AICPU_KERNEL)
    if(NOT CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
        message(FATAL_ERROR "NDS AICPU kernel is only built on an aarch64 CANN host")
    endif()
    if(NOT NDS_CANN_ROOT)
        message(FATAL_ERROR "NDS_BUILD_AICPU_KERNEL requires -DNDS_CANN_ROOT=<CANN root>")
    endif()

    set(NDS_AICPU_DEVICE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/backends/aicpu/device")
    set(NDS_AICPU_COMPILER "${NDS_CANN_ROOT}/tools/hcc/bin/aarch64-target-linux-gnu-g++")
    if(NOT EXISTS "${NDS_AICPU_COMPILER}")
        message(FATAL_ERROR "CANN AICPU shared-library compiler not found: ${NDS_AICPU_COMPILER}")
    endif()
    set(NDS_AICPU_STANDARD_KERNEL_SO "libnds_aicpu_standard.so")
    set(NDS_AICPU_OUTPUT_DIR "${CMAKE_BINARY_DIR}/aicpu")
    set(NDS_AICPU_STANDARD_KERNEL "${NDS_AICPU_OUTPUT_DIR}/${NDS_AICPU_STANDARD_KERNEL_SO}")
    set(NDS_AICPU_STANDARD_CONFIG "${NDS_AICPU_OUTPUT_DIR}/nds_aicpu_standard.json")
    set(NDS_AICPU_STANDARD_PACKAGE "${NDS_AICPU_OUTPUT_DIR}/aicpu_nds.tar.gz")
    add_custom_command(
        OUTPUT "${NDS_AICPU_STANDARD_KERNEL}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${NDS_AICPU_OUTPUT_DIR}"
        COMMAND "${NDS_AICPU_COMPILER}" -x c++ -shared -fPIC -O2 -std=c++17
            -D_GLIBCXX_USE_CXX11_ABI=1 -D_FORTIFY_SOURCE=2 -D_GNU_SOURCE -fvisibility=hidden -fstack-protector-strong
            -fno-common -fno-strict-aliasing -Wall -Wextra -Werror -I${NDS_AICPU_DEVICE_DIR}
            -I${CMAKE_CURRENT_SOURCE_DIR}/backends/aicpu
            -I${CMAKE_CURRENT_SOURCE_DIR}/backends/common
            -I${CMAKE_CURRENT_SOURCE_DIR}/include
            -I${PROJECT_SOURCE_DIR}/src/include
            ${NDS_AICPU_DEVICE_DIR}/entrypoints.cc
            ${NDS_AICPU_DEVICE_DIR}/verbs.cc
            ${NDS_AICPU_DEVICE_DIR}/transport.cc
            ${NDS_AICPU_DEVICE_DIR}/storage.cc
            -ldl -Wl,-Bsymbolic-functions,-z,relro,-z,now,-z,noexecstack -o ${NDS_AICPU_STANDARD_KERNEL}
        DEPENDS "${NDS_AICPU_DEVICE_DIR}/entrypoints.cc"
            "${NDS_AICPU_DEVICE_DIR}/verbs.cc"
            "${NDS_AICPU_DEVICE_DIR}/transport.cc"
            "${NDS_AICPU_DEVICE_DIR}/storage.cc"
            "${NDS_AICPU_DEVICE_DIR}/api.h"
            "${NDS_AICPU_DEVICE_DIR}/internal.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/backends/aicpu/launch_args.hh"
            "${NDS_AICPU_DEVICE_DIR}/hns.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/include/device_transport.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/include/device_verbs.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/backends/common/hns_hw.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/include/device_storage.h"
            "${PROJECT_SOURCE_DIR}/src/include/storage_protocol.hh"
        VERBATIM)
    configure_file(${NDS_AICPU_DEVICE_DIR}/package/nds_aicpu_standard.json.in "${NDS_AICPU_STANDARD_CONFIG}" @ONLY)
    add_custom_command(
        OUTPUT "${NDS_AICPU_STANDARD_PACKAGE}"
        COMMAND "${CMAKE_COMMAND}" -DINPUT_SO=${NDS_AICPU_STANDARD_KERNEL}
            -DSTAGE_ROOT=${NDS_AICPU_OUTPUT_DIR}/standard_package
            -DOUTPUT_PACKAGE=${NDS_AICPU_STANDARD_PACKAGE}
            -P ${NDS_AICPU_DEVICE_DIR}/package/build_standard_package.cmake
        DEPENDS "${NDS_AICPU_STANDARD_KERNEL}"
            "${NDS_AICPU_DEVICE_DIR}/package/build_standard_package.cmake"
        VERBATIM)
    add_custom_target(nds_aicpu_kernel ALL DEPENDS "${NDS_AICPU_STANDARD_KERNEL}"
        "${NDS_AICPU_STANDARD_CONFIG}" "${NDS_AICPU_STANDARD_PACKAGE}")
endif()

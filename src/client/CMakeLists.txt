add_library(nds_ra_loader STATIC
    backend/support/loaders/ra_loader.cc
    backend/support/loaders/runtime_loader.cc
    backend/support/loaders/acl_loader.cc)
target_include_directories(nds_ra_loader PUBLIC backend/support/include)
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
        target_link_options(nds_ra_loader INTERFACE "-Wl,-rpath,${NDS_CANN_LIBRARY_DIR}")
    endif()
endif()

add_library(nds_npu_ra_context STATIC backend/support/core/npu_ra_context.cc)
target_include_directories(nds_npu_ra_context PUBLIC backend/support/include)
target_link_libraries(nds_npu_ra_context PUBLIC nds_ra_loader)
target_compile_options(nds_npu_ra_context PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(nds_npu_ra_qp STATIC backend/support/core/npu_ra_qp.cc)
target_include_directories(nds_npu_ra_qp PUBLIC backend/support/include)
target_include_directories(nds_npu_ra_qp PUBLIC device/include)
target_link_libraries(nds_npu_ra_qp PUBLIC nds_ra_loader nds_transport)
target_compile_options(nds_npu_ra_qp PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(nds_host_ra STATIC backend/host_ra/backend.cc)
target_include_directories(nds_host_ra PUBLIC backend/host_ra/include)
target_link_libraries(nds_host_ra PUBLIC nds_npu_ra_context nds_npu_ra_qp nds_logging)
target_compile_options(nds_host_ra PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(nds_aiv_launcher STATIC device/aiv/launcher.cc)
target_include_directories(nds_aiv_launcher PUBLIC device/aiv/include device/include backend/support/include)
target_link_libraries(nds_aiv_launcher PUBLIC nds_ra_loader)
target_compile_options(nds_aiv_launcher PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(nds_aicpu_launcher STATIC device/aicpu/launcher.cc)
target_include_directories(nds_aicpu_launcher PUBLIC device/aicpu/include device/include backend/support/include)
target_link_libraries(nds_aicpu_launcher PUBLIC nds_ra_loader)
target_compile_options(nds_aicpu_launcher PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(nds_client_rma STATIC rma.cc)
target_include_directories(nds_client_rma PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(nds_client_rma PUBLIC
    nds_npu_ra_context
    nds_npu_ra_qp
    nds_host_ra
    nds_aiv_launcher
    nds_aicpu_launcher)
target_compile_options(nds_client_rma PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(nds_client_transport STATIC transport.cc)
target_include_directories(nds_client_transport PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(nds_client_transport PUBLIC nds_client_rma nds_transport)
target_compile_options(nds_client_transport PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(nds_client_storage STATIC storage.cc)
target_include_directories(nds_client_storage PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(nds_client_storage PUBLIC nds_client_transport nds_protocol)
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

    set(NDS_AIV_KERNEL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/device/aiv/kernel")
    set(NDS_AIV_DEVICE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/device/aiv/device")
    set(NDS_AIV_OUTPUT_DIR "${CMAKE_BINARY_DIR}/aiv")
    set(NDS_AIV_OBJECT "${NDS_AIV_OUTPUT_DIR}/nds_aiv_kernel.o")
    set(NDS_AIV_DATAPLANE_OBJECT "${NDS_AIV_OUTPUT_DIR}/nds_aiv_dataplane.o")
    set(NDS_AIV_ENTRY_OBJECT "${NDS_AIV_OUTPUT_DIR}/nds_aiv_entry.o")
    add_custom_command(
        OUTPUT "${NDS_AIV_OBJECT}"
        BYPRODUCTS "${NDS_AIV_DATAPLANE_OBJECT}" "${NDS_AIV_ENTRY_OBJECT}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${NDS_AIV_OUTPUT_DIR}"
        COMMAND "${NDS_CCEC}" -c -x cce -O2 --cce-aicore-arch=dav-c220-vec --cce-aicore-only -std=c++17
            -DTILING_KEY_VAR=0
            -I${NDS_CANN_ROOT}/aarch64-linux/asc/include -I${NDS_CANN_ROOT}/aarch64-linux/asc
            -I${NDS_CANN_ROOT}/aarch64-linux/asc/include/interface -I${NDS_CANN_ROOT}/aarch64-linux/asc/include/basic_api
            -I${NDS_CANN_ROOT}/aarch64-linux/asc/include/micro_api -I${NDS_CANN_ROOT}/aarch64-linux/asc/impl
            -I${NDS_CANN_ROOT}/aarch64-linux/asc/impl/basic_api -I${NDS_CANN_ROOT}/aarch64-linux/asc/impl/micro_api
            -I${NDS_CANN_ROOT}/aarch64-linux/ascendc/include -I${NDS_CANN_ROOT}/aarch64-linux/ascendc/include/highlevel_api
            -I${CMAKE_CURRENT_SOURCE_DIR}/device/aiv/include -I${NDS_AIV_DEVICE_DIR}/include
            -I${CMAKE_CURRENT_SOURCE_DIR}/device/include
            ${NDS_AIV_DEVICE_DIR}/nds_aiv_dataplane.cc -o ${NDS_AIV_DATAPLANE_OBJECT}
        COMMAND "${NDS_CCEC}" -c -x cce -O2 --cce-aicore-arch=dav-c220-vec --cce-aicore-only -std=c++17
            -I${NDS_CANN_ROOT}/aarch64-linux/asc/include -I${NDS_CANN_ROOT}/aarch64-linux/asc
            -I${NDS_CANN_ROOT}/aarch64-linux/asc/include/interface -I${NDS_CANN_ROOT}/aarch64-linux/asc/include/basic_api
            -I${NDS_CANN_ROOT}/aarch64-linux/asc/include/micro_api -I${NDS_CANN_ROOT}/aarch64-linux/asc/impl
            -I${NDS_CANN_ROOT}/aarch64-linux/asc/impl/basic_api -I${NDS_CANN_ROOT}/aarch64-linux/asc/impl/micro_api
            -I${NDS_CANN_ROOT}/aarch64-linux/ascendc/include -I${NDS_CANN_ROOT}/aarch64-linux/ascendc/include/highlevel_api
            -I${CMAKE_CURRENT_SOURCE_DIR}/device/aiv/include -I${NDS_AIV_DEVICE_DIR}/include
            -I${CMAKE_CURRENT_SOURCE_DIR}/device/include
            ${NDS_AIV_KERNEL_DIR}/nds_aiv_kernel.cc -o ${NDS_AIV_ENTRY_OBJECT}
        COMMAND "${NDS_AIV_LD}" -m aicorelinux -Ttext=0 "${NDS_AIV_ENTRY_OBJECT}" -static -o "${NDS_AIV_OBJECT}"
        DEPENDS "${NDS_AIV_KERNEL_DIR}/nds_aiv_kernel.cc" "${NDS_AIV_DEVICE_DIR}/nds_aiv_dataplane.cc"
            "${NDS_AIV_DEVICE_DIR}/include/nds/aiv_device_api.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/device/include/nds/device_operations.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/device/include/nds/device_connection.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/device/include/nds/device_verbs.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/device/include/nds/device_qp.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/device/include/nds/device_hns_codec.h"
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

    set(NDS_AICPU_DEVICE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/device/aicpu/device")
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
            -fno-common -fno-strict-aliasing -Wall -Wextra -Werror -I${NDS_AICPU_DEVICE_DIR}/include
            -I${CMAKE_CURRENT_SOURCE_DIR}/device/aicpu/include
            -I${CMAKE_CURRENT_SOURCE_DIR}/device/include
            ${NDS_AICPU_DEVICE_DIR}/nds_aicpu_kernel.cc
            ${NDS_AICPU_DEVICE_DIR}/nds_aicpu_dataplane.cc
            -ldl -Wl,-Bsymbolic-functions,-z,relro,-z,now,-z,noexecstack -o ${NDS_AICPU_STANDARD_KERNEL}
        DEPENDS "${NDS_AICPU_DEVICE_DIR}/nds_aicpu_kernel.cc"
            "${NDS_AICPU_DEVICE_DIR}/nds_aicpu_dataplane.cc"
            "${NDS_AICPU_DEVICE_DIR}/include/nds_aicpu_device_api.h"
            "${NDS_AICPU_DEVICE_DIR}/include/nds_aicpu_hns_abi.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/device/include/nds/device_operations.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/device/include/nds/device_connection.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/device/include/nds/device_verbs.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/device/include/nds/device_qp.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/device/include/nds/device_hns_codec.h"
        VERBATIM)
    configure_file(${NDS_AICPU_DEVICE_DIR}/package/nds_aicpu_standard.json.in "${NDS_AICPU_STANDARD_CONFIG}" @ONLY)
    add_custom_command(
        OUTPUT "${NDS_AICPU_STANDARD_PACKAGE}"
        COMMAND "${CMAKE_COMMAND}" -DINPUT_SO=${NDS_AICPU_STANDARD_KERNEL}
            -DSTAGE_ROOT=${NDS_AICPU_OUTPUT_DIR}/standard_package -DOUTPUT_PACKAGE=${NDS_AICPU_STANDARD_PACKAGE}
            -P ${NDS_AICPU_DEVICE_DIR}/package/build_standard_package.cmake
        DEPENDS "${NDS_AICPU_STANDARD_KERNEL}"
        VERBATIM)
    add_custom_target(nds_aicpu_kernel ALL DEPENDS "${NDS_AICPU_STANDARD_KERNEL}"
        "${NDS_AICPU_STANDARD_CONFIG}" "${NDS_AICPU_STANDARD_PACKAGE}")
endif()

add_executable(nds_client main.cc)
target_link_libraries(nds_client PRIVATE
    CLI11::CLI11
    nds_logging
    nds_client_storage)
target_compile_options(nds_client PRIVATE -Wall -Wextra -Wpedantic -Werror)

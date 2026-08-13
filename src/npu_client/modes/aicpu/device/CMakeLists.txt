if(NOT CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
    message(FATAL_ERROR "NDS AICPU kernel is only built on an aarch64 CANN host")
endif()
if(NOT NDS_CANN_ROOT)
    message(FATAL_ERROR "NDS_BUILD_AICPU_KERNEL requires -DNDS_CANN_ROOT=<CANN root>")
endif()

set(NDS_AICPU_COMPILER "${NDS_CANN_ROOT}/tools/hcc/bin/aarch64-target-linux-gnu-g++")
if(NOT EXISTS "${NDS_AICPU_COMPILER}")
    message(FATAL_ERROR "CANN AICPU shared-library compiler not found: ${NDS_AICPU_COMPILER}")
endif()

set(NDS_AICPU_STANDARD_KERNEL_SO "libnds_aicpu_roce_standard.so")
set(NDS_AICPU_OUTPUT_DIR "${CMAKE_BINARY_DIR}/aicpu")
set(NDS_AICPU_STANDARD_KERNEL "${NDS_AICPU_OUTPUT_DIR}/${NDS_AICPU_STANDARD_KERNEL_SO}")
set(NDS_AICPU_STANDARD_CONFIG "${NDS_AICPU_OUTPUT_DIR}/nds_aicpu_standard.json")
set(NDS_AICPU_STANDARD_PACKAGE "${NDS_AICPU_OUTPUT_DIR}/aicpu_nds.tar.gz")

function(nds_add_aicpu_library output source)
    set(extra_flags ${ARGN})
    add_custom_command(
        OUTPUT "${output}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${NDS_AICPU_OUTPUT_DIR}"
        COMMAND "${NDS_AICPU_COMPILER}"
            -x c++ -shared -fPIC -O2 -std=c++17
            ${extra_flags}
            -D_GLIBCXX_USE_CXX11_ABI=1 -D_FORTIFY_SOURCE=2 -D_GNU_SOURCE
            -fvisibility=hidden -fstack-protector-strong -fno-common -fno-strict-aliasing
            -Wall -Wextra -Werror
            -I${CMAKE_CURRENT_SOURCE_DIR}/include
            ${source}
            -ldl
            -Wl,-z,relro,-z,now,-z,noexecstack
            -o ${output}
        DEPENDS
            "${source}"
            "${CMAKE_CURRENT_SOURCE_DIR}/include/nds_aicpu_roce_abi.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/include/nds_aicpu_hns_abi.h"
        VERBATIM)
endfunction()

nds_add_aicpu_library("${NDS_AICPU_STANDARD_KERNEL}" "${CMAKE_CURRENT_SOURCE_DIR}/nds_aicpu_rdma_post.aicpu")

configure_file(package/nds_aicpu_standard.json.in "${NDS_AICPU_STANDARD_CONFIG}" @ONLY)

add_custom_command(
    OUTPUT "${NDS_AICPU_STANDARD_PACKAGE}"
    COMMAND "${CMAKE_COMMAND}"
        -DINPUT_SO=${NDS_AICPU_STANDARD_KERNEL}
        -DSTAGE_ROOT=${NDS_AICPU_OUTPUT_DIR}/standard_package
        -DOUTPUT_PACKAGE=${NDS_AICPU_STANDARD_PACKAGE}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/package/build_standard_package.cmake
    DEPENDS "${NDS_AICPU_STANDARD_KERNEL}"
    VERBATIM)

add_custom_target(nds_aicpu_kernel ALL DEPENDS
    "${NDS_AICPU_STANDARD_KERNEL}"
    "${NDS_AICPU_STANDARD_CONFIG}"
    "${NDS_AICPU_STANDARD_PACKAGE}")

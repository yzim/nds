if(NOT CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
    message(FATAL_ERROR "NDS AICPU kernel is only built on an aarch64 CANN host")
endif()
if(NOT NDS_CANN_ROOT)
    message(FATAL_ERROR "NDS_BUILD_AICPU_KERNEL requires -DNDS_CANN_ROOT=<CANN root>")
endif()

# CANN's custom AICPU-kernel template builds an ordinary ARM shared object.
# `bisheng -x aicpu` produces a relocatable embedded-device object, which is
# not the DYN ELF artifact that aclrtBinaryLoadFromFile loads for a custom
# AICPU kernel package. Use CANN's matching aarch64 host compiler. The
# kernel's dlopen/dlsym calls execute only after ACL loads this library into
# the AICPU device environment; the NPU host client does not perform them.
set(NDS_AICPU_COMPILER "${NDS_CANN_ROOT}/tools/hcc/bin/aarch64-target-linux-gnu-g++")
if(NOT EXISTS "${NDS_AICPU_COMPILER}")
    message(FATAL_ERROR "CANN AICPU shared-library compiler not found: ${NDS_AICPU_COMPILER}")
endif()

set(NDS_AICPU_KERNEL_SO "libnds_aicpu_roce.so")
set(NDS_AICPU_OUTPUT_DIR "${CMAKE_BINARY_DIR}/aicpu")
set(NDS_AICPU_KERNEL "${NDS_AICPU_OUTPUT_DIR}/${NDS_AICPU_KERNEL_SO}")
set(NDS_AICPU_CONFIG "${NDS_AICPU_OUTPUT_DIR}/nds_aicpu_roce.json")

add_custom_command(
    OUTPUT "${NDS_AICPU_KERNEL}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${NDS_AICPU_OUTPUT_DIR}"
    COMMAND "${NDS_AICPU_COMPILER}"
        -x c++ -shared -fPIC -O2 -std=c++17
        -D_GLIBCXX_USE_CXX11_ABI=1 -D_FORTIFY_SOURCE=2 -D_GNU_SOURCE
        -fvisibility=hidden -fstack-protector-strong -fno-common -fno-strict-aliasing
        -Wall -Wextra -Werror
        -I${CMAKE_CURRENT_SOURCE_DIR}/include
        -I${CMAKE_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src/nds_aicpu_rdma_post.aicpu
        -ldl
        -Wl,-z,relro,-z,now,-z,noexecstack
        -o ${NDS_AICPU_KERNEL}
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/nds_aicpu_rdma_post.aicpu"
        "${CMAKE_SOURCE_DIR}/include/nds/aicpu_roce_abi.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/nds_aicpu_hns_abi.h"
    VERBATIM)

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/package/nds_aicpu_roce.json.in" "${NDS_AICPU_CONFIG}" @ONLY)
add_custom_target(nds_aicpu_kernel ALL DEPENDS "${NDS_AICPU_KERNEL}" "${NDS_AICPU_CONFIG}")

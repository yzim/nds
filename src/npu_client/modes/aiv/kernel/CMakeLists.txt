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

set(NDS_AIV_OUTPUT_DIR "${CMAKE_BINARY_DIR}/aiv")
set(NDS_AIV_OBJECT "${NDS_AIV_OUTPUT_DIR}/nds_aiv_roce.o")
set(NDS_AIV_RELOCATABLE "${NDS_AIV_OUTPUT_DIR}/nds_aiv_roce.reloc.o")

add_custom_command(
    OUTPUT "${NDS_AIV_OBJECT}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${NDS_AIV_OUTPUT_DIR}"
    COMMAND "${NDS_CCEC}" -c -x cce -O2
        --cce-aicore-arch=dav-c220-vec
        --cce-aicore-only
        -std=c++17
        -I${NDS_CANN_ROOT}/aarch64-linux/asc/include
        -I${NDS_CANN_ROOT}/aarch64-linux/asc
        -I${NDS_CANN_ROOT}/aarch64-linux/asc/include/interface
        -I${NDS_CANN_ROOT}/aarch64-linux/asc/include/basic_api
        -I${NDS_CANN_ROOT}/aarch64-linux/asc/include/micro_api
        -I${NDS_CANN_ROOT}/aarch64-linux/asc/impl
        -I${NDS_CANN_ROOT}/aarch64-linux/asc/impl/basic_api
        -I${NDS_CANN_ROOT}/aarch64-linux/asc/impl/micro_api
        -I${NDS_CANN_ROOT}/aarch64-linux/ascendc/include
        -I${NDS_CANN_ROOT}/aarch64-linux/ascendc/include/highlevel_api
        -I${CMAKE_CURRENT_SOURCE_DIR}/../include
        ${CMAKE_CURRENT_SOURCE_DIR}/nds_aiv_roce.cpp
        -o ${NDS_AIV_RELOCATABLE}
    COMMAND "${NDS_AIV_LD}" -m aicorelinux -Ttext=0 "${NDS_AIV_RELOCATABLE}" -static -o "${NDS_AIV_OBJECT}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/nds_aiv_roce.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/../include/nds/aiv_roce_abi.h"
    VERBATIM)

add_custom_target(nds_aiv_kernel ALL DEPENDS "${NDS_AIV_OBJECT}")

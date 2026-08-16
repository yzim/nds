cmake_minimum_required(VERSION 3.20)
project(nds LANGUAGES CXX)

include(CTest)
include(FetchContent)

# spdlog releases before 1.12 require the fmt 8/9 format API. Resolve fmt
# first so CMake cannot pair a system spdlog with an incompatible newer copy.
find_package(fmt 8...<10 CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(CLI11 CONFIG REQUIRED)

set(EXPECTED_BUILD_TESTS OFF CACHE BOOL "Disable tl::expected's upstream tests" FORCE)
FetchContent_Declare(
    tl_expected
    GIT_REPOSITORY https://github.com/TartanLlama/expected.git
    GIT_TAG 292eff8bd8ee230a7df1d6a1c00c4ea0eb2f0362 # v1.1.0
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(tl_expected)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Keep the established build/<artifact> interface while subdirectories own targets.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

option(NDS_LINK_PUBLIC_ACL "Link the public AscendCL API instead of resolving it at runtime" OFF)
option(NDS_BUILD_AICPU_KERNEL "Build the NDS-owned AICPU RoCE package (aarch64 CANN host only)" OFF)
option(NDS_BUILD_AIV_KERNEL "Build the NDS-owned AIV RoCE binary (aarch64 CANN host only)" OFF)
option(NDS_BUILD_HARDWARE_PROBES "Build opt-in NPU/RNIC dataplane probes" OFF)
set(NDS_CANN_ROOT "" CACHE PATH "CANN root required by public ACL or device-kernel builds")

add_subdirectory(src/common)
add_subdirectory(src/client)
add_subdirectory(src/server)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

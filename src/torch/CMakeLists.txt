add_library(_nds_torch MODULE nds_torch.cc)
find_library(NDS_TORCH_PYTHON_LIBRARY NAMES torch_python HINTS "${TORCH_INSTALL_PREFIX}/lib" REQUIRED)
get_filename_component(NDS_TORCH_SITE_PACKAGES "${TORCH_INSTALL_PREFIX}" DIRECTORY)
find_library(NDS_TORCH_NPU_LIBRARY NAMES torch_npu HINTS "${NDS_TORCH_SITE_PACKAGES}/torch_npu/lib" REQUIRED)
target_include_directories(_nds_torch PRIVATE
    ${PROJECT_SOURCE_DIR}/src/client/backends
    ${PROJECT_SOURCE_DIR}/src/client
    ${PROJECT_SOURCE_DIR}/src/include
    "${NDS_TORCH_SITE_PACKAGES}/torch_npu/include")
target_link_libraries(_nds_torch PRIVATE
    nds_client_storage
    nds_ra
    Python3::Module
    ${NDS_TORCH_PYTHON_LIBRARY}
    ${NDS_TORCH_NPU_LIBRARY}
    ${TORCH_LIBRARIES})
target_compile_options(_nds_torch PRIVATE -Wall -Wextra -Wpedantic -Werror -Wno-unused-function ${TORCH_CXX_FLAGS})
target_compile_definitions(_nds_torch PRIVATE TORCH_EXTENSION_NAME=_nds_torch)
set_target_properties(_nds_torch PROPERTIES PREFIX "" LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

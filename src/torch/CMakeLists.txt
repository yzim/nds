add_library(nds_torch MODULE nds_torch.cc)
find_library(NDS_TORCH_PYTHON_LIBRARY NAMES torch_python HINTS "${TORCH_INSTALL_PREFIX}/lib" REQUIRED)
target_include_directories(nds_torch PRIVATE
    ${PROJECT_SOURCE_DIR}/src/client/execution
    ${PROJECT_SOURCE_DIR}/src/client/resource
    ${PROJECT_SOURCE_DIR}/src/include)
target_link_libraries(nds_torch PRIVATE
    nds_client_storage
    nds_ra
    Python3::Module
    ${NDS_TORCH_PYTHON_LIBRARY}
    ${TORCH_LIBRARIES})
target_compile_options(nds_torch PRIVATE -Wall -Wextra -Wpedantic -Werror ${TORCH_CXX_FLAGS})
target_compile_definitions(nds_torch PRIVATE TORCH_EXTENSION_NAME=nds_torch)
set_target_properties(nds_torch PROPERTIES PREFIX "")

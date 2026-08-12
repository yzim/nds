add_library(nds_ra_loader STATIC
    ra_loader.cpp
    runtime_loader.cpp
    acl_loader.cpp
    tsd_loader.cpp)
target_include_directories(nds_ra_loader PUBLIC ../include)
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

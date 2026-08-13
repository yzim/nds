add_library(nds_protocol STATIC
    rdma_wire.cc
    storage_protocol.cc)
target_include_directories(nds_protocol PUBLIC include)
target_compile_options(nds_protocol PRIVATE -Wall -Wextra -Wpedantic -Werror)

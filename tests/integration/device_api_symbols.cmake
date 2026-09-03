if(NOT DEFINED NDS_NM OR NDS_NM STREQUAL "")
    message(FATAL_ERROR "device API symbol test requires an nm tool")
endif()
if(NOT DEFINED NDS_ARTIFACTS)
    if(NOT DEFINED NDS_ARTIFACT)
        message(FATAL_ERROR "device API symbol test requires NDS_ARTIFACT or NDS_ARTIFACTS")
    endif()
    set(NDS_ARTIFACTS "${NDS_ARTIFACT}")
endif()
foreach(artifact IN LISTS NDS_ARTIFACTS)
    if(NOT EXISTS "${artifact}")
        message(FATAL_ERROR "device API artifact does not exist: ${artifact}")
    endif()
endforeach()
if(NDS_DEVICE_KIND STREQUAL "aicpu")
    set(nm_args -D -g)
    set(symbols
        nds_aicpu_post_send_kernel nds_aicpu_post_recv_kernel nds_aicpu_poll_cq_kernel
        nds_aicpu_rdma_send_kernel nds_aicpu_rdma_recv_kernel nds_aicpu_rdma_read_kernel nds_aicpu_rdma_write_kernel
        nds_aicpu_storage_bootstrap_kernel nds_aicpu_storage_read_kernel nds_aicpu_storage_write_kernel
        nds_aicpu_storage_batch_read_kernel nds_aicpu_storage_batch_write_kernel)
elseif(NDS_DEVICE_KIND STREQUAL "aiv")
    set(nm_args -g)
    set(symbols
        nds_aiv_post_send_kernel nds_aiv_post_send_batch_kernel nds_aiv_post_recv_kernel nds_aiv_poll_cq_kernel
        nds_aiv_rdma_send_kernel nds_aiv_rdma_recv_kernel nds_aiv_rdma_read_kernel nds_aiv_rdma_write_kernel
        nds_aiv_storage_bootstrap_kernel nds_aiv_storage_read_kernel nds_aiv_storage_write_kernel
        nds_aiv_storage_batch_read_kernel nds_aiv_storage_batch_write_kernel)
else()
    message(FATAL_ERROR "unknown device API kind: ${NDS_DEVICE_KIND}")
endif()

set(nm_output "")
foreach(artifact IN LISTS NDS_ARTIFACTS)
    execute_process(
        COMMAND "${NDS_NM}" ${nm_args} "${artifact}"
        RESULT_VARIABLE nm_result
        OUTPUT_VARIABLE artifact_output
        ERROR_VARIABLE nm_error)
    if(NOT nm_result EQUAL 0)
        message(FATAL_ERROR "nm failed for ${artifact}: ${nm_error}")
    endif()
    string(APPEND nm_output "${artifact_output}")
endforeach()
foreach(symbol IN LISTS symbols)
    string(FIND "${nm_output}" " ${symbol}" symbol_offset)
    if(symbol_offset EQUAL -1)
        message(FATAL_ERROR "device API artifacts do not expose ${symbol}")
    endif()
endforeach()

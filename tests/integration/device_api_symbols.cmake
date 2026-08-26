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
        NdsAicpuPostSend NdsAicpuPostRecv NdsAicpuPollCq
        NdsAicpuRdmaSend NdsAicpuRdmaRecv NdsAicpuRdmaRead NdsAicpuRdmaWrite
        NdsAicpuStorageRead NdsAicpuStorageWrite
        NdsAicpuStorageBatchRead NdsAicpuStorageBatchWrite)
elseif(NDS_DEVICE_KIND STREQUAL "aiv")
    set(nm_args -g)
    set(symbols
        NdsAivPostSend NdsAivPostSendBatch NdsAivPostRecv NdsAivPollCq
        NdsAivRdmaSend NdsAivRdmaRecv NdsAivRdmaRead NdsAivRdmaWrite
        NdsAivStorageRead NdsAivStorageWrite
        NdsAivStorageBatchRead NdsAivStorageBatchWrite)
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

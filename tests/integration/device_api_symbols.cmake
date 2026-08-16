if(NOT DEFINED NDS_ARTIFACT OR NOT EXISTS "${NDS_ARTIFACT}")
    message(FATAL_ERROR "device API artifact does not exist: ${NDS_ARTIFACT}")
endif()
if(NOT DEFINED NDS_NM OR NDS_NM STREQUAL "")
    message(FATAL_ERROR "device API symbol test requires an nm tool")
endif()
if(NDS_DEVICE_KIND STREQUAL "aicpu")
    set(nm_args -D -g "${NDS_ARTIFACT}")
    set(symbols
        NdsAicpuPostSend NdsAicpuPostRecv NdsAicpuPollCq
        NdsAicpuRdmaSend NdsAicpuRdmaRecv NdsAicpuRdmaRead NdsAicpuRdmaWrite
        NdsAicpuConnectionOp)
elseif(NDS_DEVICE_KIND STREQUAL "aiv")
    set(nm_args -g "${NDS_ARTIFACT}")
    set(symbols
        NdsAivPostSend NdsAivPostRecv NdsAivPollCq
        NdsAivRdmaSend NdsAivRdmaRecv NdsAivRdmaRead NdsAivRdmaWrite)
else()
    message(FATAL_ERROR "unknown device API kind: ${NDS_DEVICE_KIND}")
endif()

execute_process(
    COMMAND "${NDS_NM}" ${nm_args}
    RESULT_VARIABLE nm_result
    OUTPUT_VARIABLE nm_output
    ERROR_VARIABLE nm_error)
if(NOT nm_result EQUAL 0)
    message(FATAL_ERROR "nm failed for ${NDS_ARTIFACT}: ${nm_error}")
endif()
foreach(symbol IN LISTS symbols)
    string(FIND "${nm_output}" " ${symbol}" symbol_offset)
    if(symbol_offset EQUAL -1)
        message(FATAL_ERROR "${NDS_ARTIFACT} does not expose ${symbol}")
    endif()
endforeach()

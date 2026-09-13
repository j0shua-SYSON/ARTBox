if(NOT DEFINED HOST_EXECUTABLE)
    message(FATAL_ERROR "HOST_EXECUTABLE is required")
endif()
execute_process(
    COMMAND "${HOST_EXECUTABLE}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE errors
    TIMEOUT 10
)
string(REPLACE "\r\n" "\n" output "${output}")
if(NOT result STREQUAL "0" OR NOT output STREQUAL "ARTBox ready\n" OR
   NOT errors STREQUAL "")
    message(FATAL_ERROR "Startup failed: result=${result}, stdout=[${output}], stderr=[${errors}]")
endif()

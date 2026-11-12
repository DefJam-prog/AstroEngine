if(NOT DEFINED APP OR NOT EXISTS "${APP}")
    message(FATAL_ERROR "Headless executable is missing; build the project first")
endif()

execute_process(
    COMMAND "${APP}"
    RESULT_VARIABLE exit_code
    OUTPUT_VARIABLE output
    ERROR_VARIABLE errors
    TIMEOUT 10
)
if(NOT "${exit_code}" STREQUAL "0")
    message(FATAL_ERROR "Headless startup failed (${exit_code}): ${errors}")
endif()
if(NOT "${errors}" STREQUAL "")
    message(FATAL_ERROR "Unexpected stderr: ${errors}")
endif()
foreach(expected IN ITEMS "AstroEngine" "Mode: headless" "Language: C++20" "Startup complete")
    string(FIND "${output}" "${expected}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "Missing startup information: ${expected}. Output: ${output}")
    endif()
endforeach()

if(NOT DEFINED APP OR NOT EXISTS "${APP}")
    message(FATAL_ERROR "BGFX color-contract smoke app does not exist: ${APP}")
endif()
if(NOT DEFINED SCREENSHOT OR NOT DEFINED SCREENSHOT_NATIVE)
    message(FATAL_ERROR "BGFX color-contract smoke requires a build-local screenshot path")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "ASTRO_COLOR_CONTRACT_SCREENSHOT=${SCREENSHOT_NATIVE}" "${APP}"
    RESULT_VARIABLE _result OUTPUT_VARIABLE _stdout ERROR_VARIABLE _stderr TIMEOUT 40)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "BGFX color-contract screenshot smoke failed (${_result})\n${_stdout}\n${_stderr}")
endif()
foreach(_expected IN ITEMS
    "unlit_texture expected=93 mean=93,93,93 matched="
    "lit_texture expected=66 mean=66,66,66 matched="
    "unlit_vertex expected=128 mean=128,128,128 matched="
    "lit_vertex expected=66 mean=66,66,66 matched=")
    if(NOT _stdout MATCHES "${_expected}[1-9][0-9][0-9][0-9]+")
        message(FATAL_ERROR "BGFX screenshot lacks expected numeric color case '${_expected}'\n${_stdout}\n${_stderr}")
    endif()
endforeach()
if(NOT _stdout MATCHES "alpha_blend expected=92 mean=[0-9]+,[0-9]+,[0-9]+ matched=[1-9][0-9][0-9][0-9]+")
    message(FATAL_ERROR "BGFX sRGB attachment did not produce the expected alpha-blend color\n${_stdout}\n${_stderr}")
endif()
if(NOT EXISTS "${SCREENSHOT}")
    message(FATAL_ERROR "BGFX color-contract screenshot was not written: ${SCREENSHOT}\n${_stdout}\n${_stderr}")
endif()
file(SIZE "${SCREENSHOT}" _size)
if(_size LESS 100000)
    message(FATAL_ERROR "BGFX color-contract screenshot is unexpectedly small (${_size} bytes)")
endif()
message(STATUS "BGFX D3D11 screenshot matches four production shader paths; screenshot=${SCREENSHOT}; ${_stdout}")

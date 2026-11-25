if(NOT DEFINED APP OR NOT EXISTS "${APP}")
    message(FATAL_ERROR "Runtime BGFX Game View smoke app does not exist: ${APP}")
endif()
if(NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "Runtime BGFX Game View smoke requires a build-local WORK_DIR")
endif()

file(MAKE_DIRECTORY "${WORK_DIR}")
set(_screenshot "${WORK_DIR}/runtime-bgfx-game-view.tga")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "ASTRO_EDITOR_SCREENSHOT_OUTPUT=${_screenshot}"
        "${APP}" --smoke-runtime-bgfx-game-view
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _stdout
    ERROR_VARIABLE _stderr
    TIMEOUT 55)

if(NOT _result EQUAL 0)
    message(FATAL_ERROR "Editor Runtime BGFX Game View smoke failed (${_result})\n${_stdout}\n${_stderr}")
endif()
if(NOT _stdout MATCHES "runtimeBgfxGameViewAdapter=1 runtimeBgfxGameViewSubmissions=1 runtimeBgfxGameViewCache=1"
        OR NOT _stdout MATCHES "runtimeBgfxGameViewScreenshot=")
    message(FATAL_ERROR "Editor Play Game View did not use the Runtime BGFX adapter\n${_stdout}\n${_stderr}")
endif()
if(NOT EXISTS "${_screenshot}")
    message(FATAL_ERROR "Editor did not save the Runtime BGFX Game View screenshot: ${_screenshot}")
endif()
file(SIZE "${_screenshot}" _screenshot_size)
if(_screenshot_size LESS 100000)
    message(FATAL_ERROR "Runtime BGFX Game View screenshot is unexpectedly small (${_screenshot_size} bytes)")
endif()
message(STATUS "Editor Play used the Runtime BGFX adapter; screenshot=${_screenshot}")

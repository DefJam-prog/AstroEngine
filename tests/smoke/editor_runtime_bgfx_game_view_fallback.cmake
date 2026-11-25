if(NOT DEFINED APP OR NOT EXISTS "${APP}")
    message(FATAL_ERROR "Runtime BGFX Game View fallback smoke app does not exist: ${APP}")
endif()
if(NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "Runtime BGFX Game View fallback smoke requires a build-local WORK_DIR")
endif()

file(MAKE_DIRECTORY "${WORK_DIR}")
execute_process(
    COMMAND "${APP}" --smoke-runtime-bgfx-game-view-fallback
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _stdout
    ERROR_VARIABLE _stderr
    TIMEOUT 55)

if(NOT _result EQUAL 0)
    message(FATAL_ERROR "Editor Runtime BGFX Game View fallback smoke failed (${_result})\n${_stdout}\n${_stderr}")
endif()
if(NOT _stdout MATCHES "runtimeBgfxGameViewAdapter=0 runtimeBgfxGameViewSubmissions=0"
        OR NOT _stdout MATCHES "runtimeBgfxGameViewFallbackTexturedMeshes=[1-9][0-9]*")
    message(FATAL_ERROR "Editor Play did not retain textured rendering through the Editor fallback\n${_stdout}\n${_stderr}")
endif()
message(STATUS "Editor Play fell back for textured materials and submitted them through the Editor renderer")

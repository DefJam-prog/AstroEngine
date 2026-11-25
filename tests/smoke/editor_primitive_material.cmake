if(NOT DEFINED APP OR NOT EXISTS "${APP}")
    message(FATAL_ERROR "Primitive material Editor smoke app does not exist: ${APP}")
endif()
if(NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "Primitive material Editor smoke requires a build-local WORK_DIR")
endif()

file(MAKE_DIRECTORY "${WORK_DIR}")
set(_screenshot "${WORK_DIR}/scene-view-materialized-primitive.tga")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "ASTRO_EDITOR_SCREENSHOT_OUTPUT=${_screenshot}"
        "${APP}" --smoke-primitive-material
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _stdout
    ERROR_VARIABLE _stderr
    TIMEOUT 45)

if(NOT _result EQUAL 0)
    message(FATAL_ERROR "Editor primitive material screenshot smoke failed (${_result})\n${_stdout}\n${_stderr}")
endif()
if(NOT _stdout MATCHES "primitiveMaterialPixels=[1-9][0-9][0-9]+"
        OR NOT _stdout MATCHES "lastFrameMeshSubmissions=7"
        OR NOT _stdout MATCHES "lastFrameTexturedMeshes=5"
        OR NOT _stdout MATCHES "lastFramePrimitiveMaterialSubmissions=[1-9][0-9]*"
        OR NOT _stdout MATCHES "gameViewPrimitiveMaterialSubmissions=[1-9][0-9]*")
    message(FATAL_ERROR "Editor did not visibly render the assigned primitive material\n${_stdout}\n${_stderr}")
endif()
if(NOT _stdout MATCHES "primitiveMaterialPixels=([1-9][0-9]*) renderer=Direct3D 11")
    message(FATAL_ERROR "Editor primitive material smoke did not confirm the default D3D11 renderer\n${_stdout}\n${_stderr}")
endif()
set(_yellow_pixels "${CMAKE_MATCH_1}")
if(NOT EXISTS "${_screenshot}")
    message(FATAL_ERROR "Editor did not preserve the requested Scene View screenshot: ${_screenshot}")
endif()
file(SIZE "${_screenshot}" _screenshot_size)
if(_screenshot_size LESS 100000)
    message(FATAL_ERROR "Scene View screenshot is unexpectedly small (${_screenshot_size} bytes)")
endif()
message(STATUS "Editor primitive material Scene/Game View render passed; renderer=Direct3D 11 yellowPixels=${_yellow_pixels}; screenshot=${_screenshot}")

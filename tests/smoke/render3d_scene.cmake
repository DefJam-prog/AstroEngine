if(NOT DEFINED APP OR NOT EXISTS "${APP}")
    message(FATAL_ERROR "ASTRO 3D Scene smoke app does not exist: ${APP}")
endif()
if(NOT DEFINED FIXTURE_DIR OR NOT IS_DIRECTORY "${FIXTURE_DIR}")
    message(FATAL_ERROR "ASTRO 3D Scene smoke fixture is missing: ${FIXTURE_DIR}")
endif()
if(NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "ASTRO 3D Scene smoke requires a build-local WORK_DIR")
endif()

set(_project "")
foreach(_attempt RANGE 1 16)
    string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef _suffix)
    set(_candidate "${WORK_DIR}-${_suffix}")
    if(NOT EXISTS "${_candidate}")
        set(_project "${_candidate}")
        break()
    endif()
endforeach()
if(_project STREQUAL "")
    message(FATAL_ERROR "Unable to allocate a unique temporary Scene smoke project")
endif()

file(MAKE_DIRECTORY "${_project}")
file(COPY "${FIXTURE_DIR}/Assets" "${FIXTURE_DIR}/Scenes" DESTINATION "${_project}")
execute_process(
    COMMAND "${APP}" --scene-smoke --project-root "${_project}"
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _stdout
    ERROR_VARIABLE _stderr
    TIMEOUT 35)
file(REMOVE_RECURSE "${_project}")

if(NOT _result EQUAL 0)
    message(FATAL_ERROR "Scene/bgfx visual smoke failed (${_result})\n${_stdout}\n${_stderr}")
endif()
if(NOT _stdout MATCHES "scene_mesh=1 model_asset=1 mesh_index=0 primitive_index=0 visible=1")
    message(FATAL_ERROR "Scene/bgfx smoke did not report the selected Scene primitive\n${_stdout}\n${_stderr}")
endif()
if(NOT _stdout MATCHES "pixel_capture=1 foreground_pixels=[0-9]+ color_buckets=[0-9]+ bounds=[0-9]+,[0-9]+-[0-9]+,[0-9]+")
    message(FATAL_ERROR "Scene/bgfx smoke did not report a validated pixel capture\n${_stdout}\n${_stderr}")
endif()
message(STATUS "Scene/bgfx visual smoke passed: ${_stdout}")

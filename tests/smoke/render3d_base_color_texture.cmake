if(NOT DEFINED APP OR NOT EXISTS "${APP}")
    message(FATAL_ERROR "ASTRO base-color texture smoke app does not exist: ${APP}")
endif()
if(NOT DEFINED FIXTURE_DIR OR NOT IS_DIRECTORY "${FIXTURE_DIR}")
    message(FATAL_ERROR "ASTRO base-color texture fixture is missing: ${FIXTURE_DIR}")
endif()
if(NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "ASTRO base-color texture smoke requires a build-local WORK_DIR")
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
    message(FATAL_ERROR "Unable to allocate a unique base-color texture smoke project")
endif()

file(MAKE_DIRECTORY "${_project}")
file(COPY "${FIXTURE_DIR}/Assets" "${FIXTURE_DIR}/Scenes" DESTINATION "${_project}")
execute_process(
    COMMAND "${APP}" --texture-smoke --project-root "${_project}"
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _stdout
    ERROR_VARIABLE _stderr
    TIMEOUT 35)
file(REMOVE_RECURSE "${_project}")

if(NOT _result EQUAL 0)
    message(FATAL_ERROR "Base-color texture/UV0 BGFX smoke failed (${_result})\n${_stdout}\n${_stderr}")
endif()
if(NOT _stdout MATCHES "texture_smoke=1 texture_asset=2 texture_uv=0 texture_size=4x4 sampled_color_families=[2-4]")
    message(FATAL_ERROR "Smoke did not resolve the BASE_COLOR image through UV0 or sample distinct texels\n${_stdout}\n${_stderr}")
endif()
if(NOT _stdout MATCHES "pixel_capture=1 foreground_pixels=[1-9][0-9][0-9]+ color_buckets=[1-9][0-9]* bounds=[0-9]+,[0-9]+-[0-9]+,[0-9]+")
    message(FATAL_ERROR "Base-color texture smoke did not validate BGFX screenshot coverage\n${_stdout}\n${_stderr}")
endif()
message(STATUS "Base-color texture/UV0 BGFX smoke passed: ${_stdout}")

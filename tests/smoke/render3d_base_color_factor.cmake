if(NOT DEFINED APP OR NOT EXISTS "${APP}")
    message(FATAL_ERROR "ASTRO base-color factor smoke app does not exist: ${APP}")
endif()
if(NOT DEFINED FIXTURE_DIR OR NOT IS_DIRECTORY "${FIXTURE_DIR}")
    message(FATAL_ERROR "ASTRO base-color factor fixture is missing: ${FIXTURE_DIR}")
endif()
if(NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "ASTRO base-color factor smoke requires a build-local WORK_DIR")
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
    message(FATAL_ERROR "Unable to allocate a unique base-color factor smoke project")
endif()

file(MAKE_DIRECTORY "${_project}")
file(COPY "${FIXTURE_DIR}/Assets" "${FIXTURE_DIR}/Scenes" DESTINATION "${_project}")
execute_process(
    COMMAND "${APP}" --base-color-factor-smoke --project-root "${_project}"
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _stdout
    ERROR_VARIABLE _stderr
    TIMEOUT 35)
file(REMOVE_RECURSE "${_project}")

if(NOT _result EQUAL 0)
    message(FATAL_ERROR "sRGB base-color factor BGFX smoke failed (${_result})\n${_stdout}\n${_stderr}")
endif()
message(STATUS "D3D11 BGFX mid-gray factor screenshot smoke passed: ${_stdout}")

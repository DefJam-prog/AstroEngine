if(NOT DEFINED APP OR NOT EXISTS "${APP}")
    message(FATAL_ERROR "ASTRO 3D material smoke app does not exist: ${APP}")
endif()
if(NOT DEFINED FIXTURE_DIR OR NOT IS_DIRECTORY "${FIXTURE_DIR}")
    message(FATAL_ERROR "ASTRO 3D material fixture is missing: ${FIXTURE_DIR}")
endif()
if(NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "ASTRO 3D material smoke requires a build-local WORK_DIR")
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
    message(FATAL_ERROR "Unable to allocate a unique material smoke project")
endif()

file(MAKE_DIRECTORY "${_project}")
file(COPY "${FIXTURE_DIR}/Assets" "${FIXTURE_DIR}/Scenes" DESTINATION "${_project}")
execute_process(
    COMMAND "${APP}" --material-smoke --project-root "${_project}"
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _stdout
    ERROR_VARIABLE _stderr
    TIMEOUT 35)
file(REMOVE_RECURSE "${_project}")

if(NOT _result EQUAL 0)
    message(FATAL_ERROR "Material/BGFX visual smoke failed (${_result})\n${_stdout}\n${_stderr}")
endif()
if(NOT _stdout MATCHES "material_override=2"
        OR NOT _stdout MATCHES "base_color=0[.]800,0[.]120,0[.]040,1[.]000"
        OR NOT _stdout MATCHES "material_pixels=[1-9][0-9][0-9][0-9]+")
    message(FATAL_ERROR "Material smoke did not resolve and visibly apply the expected base-color factor\n${_stdout}\n${_stderr}")
endif()
if(NOT _stdout MATCHES "pixel_capture=1 foreground_pixels=[0-9]+ color_buckets=[0-9]+ bounds=[0-9]+,[0-9]+-[0-9]+,[0-9]+")
    message(FATAL_ERROR "Material smoke did not report a validated pixel capture\n${_stdout}\n${_stderr}")
endif()
message(STATUS "Material/BGFX visual smoke passed: ${_stdout}")

if(NOT DEFINED CPACK_EXECUTABLE OR NOT EXISTS "${CPACK_EXECUTABLE}")
    message(FATAL_ERROR "CPack executable was not found")
endif()
if(NOT DEFINED CPACK_CONFIG OR NOT EXISTS "${CPACK_CONFIG}")
    message(FATAL_ERROR "CPack configuration was not found")
endif()
if(NOT DEFINED PACKAGE_FILE_NAME OR NOT DEFINED TEST_ROOT)
    message(FATAL_ERROR "Package filename and test root are required")
endif()

string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef run_id)
set(run_dir "${TEST_ROOT}/${run_id}")
set(extract_dir "${run_dir}/extracted")
file(MAKE_DIRECTORY "${run_dir}" "${extract_dir}")

execute_process(
    COMMAND "${CPACK_EXECUTABLE}" --config "${CPACK_CONFIG}" -B "${run_dir}"
    RESULT_VARIABLE package_result
    OUTPUT_VARIABLE package_stdout
    ERROR_VARIABLE package_stderr
    TIMEOUT 60)
if(NOT package_result EQUAL 0)
    message(FATAL_ERROR "CPack failed (${package_result})\n${package_stdout}\n${package_stderr}")
endif()

set(package_archive "${run_dir}/${PACKAGE_FILE_NAME}")
if(NOT EXISTS "${package_archive}")
    message(FATAL_ERROR "Expected runtime archive was not created: ${package_archive}")
endif()
file(ARCHIVE_EXTRACT INPUT "${package_archive}" DESTINATION "${extract_dir}")

set(runtime_app "${extract_dir}/astro_game_ui_demo.exe")
foreach(required IN ITEMS
        "${runtime_app}"
        "${extract_dir}/README-runtime.txt"
        "${extract_dir}/licenses/SDL3-LICENSE.txt"
        "${extract_dir}/licenses/Dear-ImGui-LICENSE.txt")
    if(NOT EXISTS "${required}")
        message(FATAL_ERROR "Packaged runtime is missing: ${required}")
    endif()
endforeach()

execute_process(
    COMMAND "${runtime_app}" --smoke
    WORKING_DIRECTORY "${extract_dir}"
    RESULT_VARIABLE app_result
    OUTPUT_VARIABLE app_stdout
    ERROR_VARIABLE app_stderr
    TIMEOUT 30)
if(NOT app_result EQUAL 0)
    message(FATAL_ERROR "Packaged runtime clean-start smoke failed (${app_result})\n${app_stdout}\n${app_stderr}")
endif()

message(STATUS "Packaged runtime launched successfully from extracted ZIP: ${package_archive}")

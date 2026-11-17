cmake_minimum_required(VERSION 3.20)

foreach(_required IN ITEMS APP FIXTURE WORK_ROOT)
    if(NOT DEFINED ${_required})
        message(FATAL_ERROR "Missing -D${_required}=... argument")
    endif()
endforeach()

file(REAL_PATH "${WORK_ROOT}" _work_root)
string(RANDOM LENGTH 14 ALPHABET 0123456789abcdef _suffix)
set(_fixture_root "${_work_root}/Astro2DProjectFixture-${_suffix}")
set(_project_root "${_work_root}/Astro2DPreview-Проверка-${_suffix}")
if(EXISTS "${_fixture_root}" OR EXISTS "${_project_root}")
    message(FATAL_ERROR "Generated project test path already exists")
endif()

function(_cleanup_owned_project _path)
    set(_base "${_work_root}")
    cmake_path(IS_PREFIX _base "${_path}" NORMALIZE _is_inside)
    if(_is_inside AND EXISTS "${_path}/.astro/assets.index")
        file(REMOVE_RECURSE "${_path}")
    endif()
endfunction()

execute_process(COMMAND "${FIXTURE}" --write-fixture "${_fixture_root}"
    RESULT_VARIABLE _fixture_result OUTPUT_VARIABLE _fixture_out ERROR_VARIABLE _fixture_err)
if(NOT _fixture_result EQUAL 0)
    _cleanup_owned_project("${_fixture_root}")
    message(FATAL_ERROR "Could not create owned fixture (${_fixture_result}): ${_fixture_out}${_fixture_err}")
endif()
file(RENAME "${_fixture_root}" "${_project_root}")

set(_scene "${_project_root}/Scenes/Preview.scene")
set(_index "${_project_root}/.astro/assets.index")
set(_source "${_project_root}/Assets/sprite.png")
file(SHA256 "${_index}" _index_before)
file(SHA256 "${_scene}" _scene_before)
file(SHA256 "${_source}" _source_before)
file(SHA256 "${_source}" _asset_hash)
set(_cache "${_project_root}/Library/Assets/${_asset_hash}.v1.png")
file(SHA256 "${_cache}" _cache_before)

execute_process(COMMAND "${APP}" --project "${_project_root}" --scene "Scenes/Preview.scene" --smoke
    RESULT_VARIABLE _preview_result OUTPUT_VARIABLE _preview_out ERROR_VARIABLE _preview_err)
if(NOT _preview_result EQUAL 0)
    _cleanup_owned_project("${_project_root}")
    message(FATAL_ERROR "Project preview failed (${_preview_result}): ${_preview_out}${_preview_err}")
endif()

file(SHA256 "${_index}" _index_after)
file(SHA256 "${_scene}" _scene_after)
file(SHA256 "${_source}" _source_after)
file(SHA256 "${_cache}" _cache_after)
if(NOT _index_before STREQUAL _index_after OR NOT _scene_before STREQUAL _scene_after
    OR NOT _source_before STREQUAL _source_after OR NOT _cache_before STREQUAL _cache_after)
    _cleanup_owned_project("${_project_root}")
    message(FATAL_ERROR "Project preview modified scene, asset source, asset cache, or registry files")
endif()

execute_process(COMMAND "${APP}" --project "${_project_root}" --scene "../outside.scene" --smoke
    RESULT_VARIABLE _traversal_result OUTPUT_VARIABLE _traversal_out ERROR_VARIABLE _traversal_err)
if(_traversal_result EQUAL 0)
    _cleanup_owned_project("${_project_root}")
    message(FATAL_ERROR "Project preview accepted a scene path that traverses outside the project")
endif()

_cleanup_owned_project("${_project_root}")
message(STATUS "Loaded and rendered project Scene from a Unicode path without modifying project files")

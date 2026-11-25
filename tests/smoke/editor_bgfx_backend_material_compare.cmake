if(NOT DEFINED APP OR NOT EXISTS "${APP}")
    message(FATAL_ERROR "BGFX backend comparison requires the Editor executable: ${APP}")
endif()
if(NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "BGFX backend comparison requires a build-local WORK_DIR")
endif()

file(MAKE_DIRECTORY "${WORK_DIR}")
set(_d3d_screenshot "${WORK_DIR}/d3d11-scene-view-material.tga")
set(_vulkan_screenshot "${WORK_DIR}/vulkan-scene-view-material.tga")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "ASTRO_EDITOR_SCREENSHOT_OUTPUT=${_d3d_screenshot}"
        "${APP}" --smoke-primitive-material
    RESULT_VARIABLE _d3d_result
    OUTPUT_VARIABLE _d3d_stdout
    ERROR_VARIABLE _d3d_stderr
    TIMEOUT 45)
if(NOT _d3d_result EQUAL 0)
    message(FATAL_ERROR "D3D11 material Scene View smoke failed (${_d3d_result})\n${_d3d_stdout}\n${_d3d_stderr}")
endif()
if(NOT _d3d_stdout MATCHES "primitiveMaterialPixels=([1-9][0-9]*) renderer=Direct3D 11")
    message(FATAL_ERROR "D3D11 smoke did not report a screenshot-derived material pixel count\n${_d3d_stdout}\n${_d3d_stderr}")
endif()
set(_d3d_pixels "${CMAKE_MATCH_1}")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "ASTRO_EDITOR_SCREENSHOT_OUTPUT=${_vulkan_screenshot}"
        "${APP}" --smoke-bgfx-vulkan-scene-view-material
    RESULT_VARIABLE _vulkan_result
    OUTPUT_VARIABLE _vulkan_stdout
    ERROR_VARIABLE _vulkan_stderr
    TIMEOUT 75)
if(NOT _vulkan_result EQUAL 0)
    if(_vulkan_result EQUAL 77)
        message(STATUS "VULKAN_BACKEND_UNAVAILABLE: linked BGFX build does not expose Vulkan; comparison skipped")
        return()
    endif()
    message(FATAL_ERROR "Vulkan Scene View material smoke failed (${_vulkan_result})\n${_vulkan_stdout}\n${_vulkan_stderr}")
endif()
if(NOT _vulkan_stdout MATCHES "primitiveMaterialPixels=([1-9][0-9]*) renderer=Vulkan")
    message(FATAL_ERROR "Vulkan smoke did not report a screenshot-derived material pixel count\n${_vulkan_stdout}\n${_vulkan_stderr}")
endif()
set(_vulkan_pixels "${CMAKE_MATCH_1}")

foreach(_screenshot IN ITEMS "${_d3d_screenshot}" "${_vulkan_screenshot}")
    if(NOT EXISTS "${_screenshot}")
        message(FATAL_ERROR "Backend smoke did not preserve its requested screenshot: ${_screenshot}")
    endif()
    file(SIZE "${_screenshot}" _screenshot_size)
    if(_screenshot_size LESS 100000)
        message(FATAL_ERROR "Backend screenshot is unexpectedly small (${_screenshot_size} bytes): ${_screenshot}")
    endif()
endforeach()

# The screenshots have different window dimensions across backends. Compare the
# same localized diagnostic-material classifier count with a bounded 5% raster
# tolerance instead of requiring full-frame pixel identity.
math(EXPR _pixel_delta "${_d3d_pixels} - ${_vulkan_pixels}")
if(_pixel_delta LESS 0)
    math(EXPR _pixel_delta "0 - ${_pixel_delta}")
endif()
math(EXPR _max_pixels "${_d3d_pixels} * 5 / 100")
if(_max_pixels LESS 8)
    set(_max_pixels 8)
endif()
if(_pixel_delta GREATER _max_pixels)
    message(FATAL_ERROR
        "Localized Scene View material coverage differs beyond tolerance: D3D11=${_d3d_pixels}, Vulkan=${_vulkan_pixels}, allowed=${_max_pixels}")
endif()
message(STATUS
    "Localized Scene View material screenshot comparison passed: D3D11=${_d3d_pixels}, Vulkan=${_vulkan_pixels}, delta=${_pixel_delta}, allowed=${_max_pixels}")

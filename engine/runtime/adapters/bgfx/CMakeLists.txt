if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "astro_runtime_bgfx_scene3d currently requires Windows x64 MSVC")
endif()

add_library(astro_runtime_bgfx_scene3d BgfxSceneRenderer.cpp)
target_include_directories(astro_runtime_bgfx_scene3d
    PUBLIC "${PROJECT_SOURCE_DIR}/engine/runtime/adapters/bgfx"
        "${ASTRO_SPIKE_DEPS}/bgfx/include")
target_link_libraries(astro_runtime_bgfx_scene3d PUBLIC astro_scene_render3d PRIVATE spike_bgfx)
target_compile_features(astro_runtime_bgfx_scene3d PUBLIC cxx_std_20)
set_target_properties(astro_runtime_bgfx_scene3d PROPERTIES CXX_EXTENSIONS OFF)
target_compile_options(astro_runtime_bgfx_scene3d PRIVATE /W4 /permissive- /EHsc /Zc:__cplusplus /utf-8)

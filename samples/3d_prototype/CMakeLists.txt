if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "The current bgfx 3D smoke prototype requires Windows x64 MSVC/D3D11")
endif()
set(shader_source "${ASTRO_SPIKE_DEPS}/bgfx/examples/runtime/shaders/dxbc")
foreach(shader IN ITEMS vs_cubes.bin fs_cubes.bin)
    if(NOT EXISTS "${shader_source}/${shader}")
        message(FATAL_ERROR "Missing bgfx runtime shader ${shader_source}/${shader}; no network download is attempted")
    endif()
endforeach()

add_executable(astro_render3d_bgfx_smoke main.cpp)
target_compile_features(astro_render3d_bgfx_smoke PRIVATE cxx_std_20)
set_target_properties(astro_render3d_bgfx_smoke PROPERTIES CXX_EXTENSIONS OFF)
target_compile_options(astro_render3d_bgfx_smoke PRIVATE /W4 /permissive- /Zc:__cplusplus /Zc:preprocessor /utf-8)
target_compile_definitions(astro_render3d_bgfx_smoke PRIVATE
    BX_CONFIG_DEBUG=$<IF:$<CONFIG:Debug>,1,0>
    ASTRO_3D_SHADER_DIR="${CMAKE_CURRENT_BINARY_DIR}/shaders")
target_include_directories(astro_render3d_bgfx_smoke PRIVATE
    "${ASTRO_SPIKE_DEPS}/bgfx/include" "${ASTRO_SPIKE_DEPS}/bx/include")
target_link_libraries(astro_render3d_bgfx_smoke PRIVATE astro_runtime_render3d spike_bgfx spike_bimg spike_bx
    SDL3::SDL3-static d3d11 dxgi dxguid)
if(ASTRO_BUILD_GLTF_IMPORTER)
    target_link_libraries(astro_render3d_bgfx_smoke PRIVATE astro_gltf)
    target_compile_definitions(astro_render3d_bgfx_smoke PRIVATE ASTRO_RENDER3D_HAS_SCENE_SMOKE=1)
endif()
add_custom_command(TARGET astro_render3d_bgfx_smoke POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${shader_source}/vs_cubes.bin" "${CMAKE_CURRENT_BINARY_DIR}/shaders/vs_cubes.bin"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${shader_source}/fs_cubes.bin" "${CMAKE_CURRENT_BINARY_DIR}/shaders/fs_cubes.bin"
    VERBATIM)
if(BUILD_TESTING)
    add_test(NAME runtime.render3d.bgfx.smoke COMMAND astro_render3d_bgfx_smoke --smoke)
    set_tests_properties(runtime.render3d.bgfx.smoke PROPERTIES LABELS "smoke;graphics;3d" TIMEOUT 30 RUN_SERIAL TRUE)
    if(ASTRO_BUILD_GLTF_IMPORTER)
        add_test(NAME runtime.render3d.scene.bgfx.smoke
            COMMAND "${CMAKE_COMMAND}"
                "-DAPP=$<TARGET_FILE:astro_render3d_bgfx_smoke>"
                "-DFIXTURE_DIR=${PROJECT_SOURCE_DIR}/tests/fixtures/render3d_scene_project"
                "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/scene-smoke"
                -P "${PROJECT_SOURCE_DIR}/tests/smoke/render3d_scene.cmake")
        set_tests_properties(runtime.render3d.scene.bgfx.smoke PROPERTIES
            LABELS "smoke;graphics;3d;scene;import" TIMEOUT 45 RUN_SERIAL TRUE)
    endif()
endif()

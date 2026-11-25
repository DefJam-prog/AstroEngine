if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "The current bgfx 3D smoke prototype requires Windows x64 MSVC/D3D11")
endif()
set(shader_source "${ASTRO_SPIKE_DEPS}/bgfx/examples/runtime/shaders/dxbc")
foreach(shader IN ITEMS vs_cubes.bin fs_cubes.bin vs_stencil_texture.bin fs_stencil_texture.bin)
    if(NOT EXISTS "${shader_source}/${shader}")
        message(FATAL_ERROR "Missing bgfx runtime shader ${shader_source}/${shader}; no network download is attempted")
    endif()
endforeach()

add_executable(astro_render3d_bgfx_smoke main.cpp)
target_compile_features(astro_render3d_bgfx_smoke PRIVATE cxx_std_20)
set_target_properties(astro_render3d_bgfx_smoke PROPERTIES CXX_EXTENSIONS OFF)
target_compile_options(astro_render3d_bgfx_smoke PRIVATE /W4 /permissive- /EHsc /Zc:__cplusplus /Zc:preprocessor /utf-8)
target_compile_definitions(astro_render3d_bgfx_smoke PRIVATE
    BX_CONFIG_DEBUG=$<IF:$<CONFIG:Debug>,1,0>
    ASTRO_3D_SHADER_DIR="${CMAKE_CURRENT_BINARY_DIR}/shaders")
target_include_directories(astro_render3d_bgfx_smoke PRIVATE
    "${ASTRO_SPIKE_DEPS}/bgfx/include" "${ASTRO_SPIKE_DEPS}/bx/include")
target_link_libraries(astro_render3d_bgfx_smoke PRIVATE astro_runtime_render3d spike_bgfx spike_bimg spike_bx
    SDL3::SDL3-static d3d11 dxgi dxguid)
if(ASTRO_BUILD_GLTF_IMPORTER)
    target_link_libraries(astro_render3d_bgfx_smoke PRIVATE astro_gltf astro_runtime_sdl_wic_image)
    target_compile_definitions(astro_render3d_bgfx_smoke PRIVATE ASTRO_RENDER3D_HAS_SCENE_SMOKE=1)
endif()
if(ASTRO_BUILD_RUNTIME_BGFX_3D)
    target_link_libraries(astro_render3d_bgfx_smoke PRIVATE astro_runtime_bgfx_scene3d astro_runtime_session)
    target_compile_definitions(astro_render3d_bgfx_smoke PRIVATE ASTRO_RENDER3D_HAS_RUNTIME_PACKET=1)
endif()
add_custom_command(TARGET astro_render3d_bgfx_smoke POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${shader_source}/vs_cubes.bin" "${CMAKE_CURRENT_BINARY_DIR}/shaders/vs_cubes.bin"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${shader_source}/fs_cubes.bin" "${CMAKE_CURRENT_BINARY_DIR}/shaders/fs_cubes.bin"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${shader_source}/vs_stencil_texture.bin" "${CMAKE_CURRENT_BINARY_DIR}/shaders/vs_stencil_texture.bin"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${shader_source}/fs_stencil_texture.bin" "${CMAKE_CURRENT_BINARY_DIR}/shaders/fs_stencil_texture.bin"
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
        add_test(NAME runtime.render3d.material.bgfx.smoke
            COMMAND "${CMAKE_COMMAND}"
                "-DAPP=$<TARGET_FILE:astro_render3d_bgfx_smoke>"
                "-DFIXTURE_DIR=${PROJECT_SOURCE_DIR}/tests/fixtures/render3d_scene_project"
                "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/material-smoke"
                -P "${PROJECT_SOURCE_DIR}/tests/smoke/render3d_material.cmake")
        set_tests_properties(runtime.render3d.material.bgfx.smoke PROPERTIES
            LABELS "smoke;graphics;3d;material" TIMEOUT 45 RUN_SERIAL TRUE)
        add_test(NAME runtime.render3d.base_color_texture.bgfx.smoke
            COMMAND "${CMAKE_COMMAND}"
                "-DAPP=$<TARGET_FILE:astro_render3d_bgfx_smoke>"
                "-DFIXTURE_DIR=${PROJECT_SOURCE_DIR}/tests/fixtures/render3d_scene_project"
                "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/base-color-texture-smoke"
                -P "${PROJECT_SOURCE_DIR}/tests/smoke/render3d_base_color_texture.cmake")
        set_tests_properties(runtime.render3d.base_color_texture.bgfx.smoke PROPERTIES
            LABELS "smoke;graphics;3d;texture;assets" TIMEOUT 45 RUN_SERIAL TRUE)
        add_test(NAME runtime.render3d.base_color_factor.bgfx.smoke
            COMMAND "${CMAKE_COMMAND}"
                "-DAPP=$<TARGET_FILE:astro_render3d_bgfx_smoke>"
                "-DFIXTURE_DIR=${PROJECT_SOURCE_DIR}/tests/fixtures/render3d_scene_project"
                "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/base-color-factor-smoke"
                -P "${PROJECT_SOURCE_DIR}/tests/smoke/render3d_base_color_factor.cmake")
        set_tests_properties(runtime.render3d.base_color_factor.bgfx.smoke PROPERTIES
            LABELS "smoke;graphics;3d;texture;color" TIMEOUT 45 RUN_SERIAL TRUE)

        set(_astro_color_shader_source "${ASTRO_SPIKE_DEPS}/bgfx/examples/runtime/shaders/dxbc")
        set(_astro_color_shaderc "${ASTRO_SPIKE_DEPS}/bgfx/.build/win64_vs2022/bin/shadercRelease.exe")
        if(NOT EXISTS "${_astro_color_shaderc}")
            set(_astro_color_shaderc "${ASTRO_SPIKE_DEPS}/bgfx/.build/win64_vs2022/bin/shaderc.exe")
        endif()
        if(NOT EXISTS "${_astro_color_shaderc}")
            message(FATAL_ERROR "BGFX color-contract smoke needs the already configured local shaderc")
        endif()
        set(_astro_color_shader_dir "${CMAKE_CURRENT_BINARY_DIR}/color-contract-shaders")
        set(_astro_color_shader_outputs
            "${_astro_color_shader_dir}/fs_scene_color.bin"
            "${_astro_color_shader_dir}/fs_scene_textured.bin"
            "${_astro_color_shader_dir}/vs_scene_lit_color.bin"
            "${_astro_color_shader_dir}/fs_scene_lit_color.bin"
            "${_astro_color_shader_dir}/vs_scene_lit_textured.bin"
            "${_astro_color_shader_dir}/fs_scene_lit_textured.bin")
        add_custom_command(OUTPUT ${_astro_color_shader_outputs}
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${_astro_color_shader_dir}"
            COMMAND "${_astro_color_shaderc}" -f "${PROJECT_SOURCE_DIR}/editor/shaders/scene_color.sc"
                -o "${_astro_color_shader_dir}/fs_scene_color.bin" --type fragment
                --platform windows -p s_5_0 --varyingdef "${PROJECT_SOURCE_DIR}/editor/shaders/scene_color.varying.def.sc"
                -i "${ASTRO_SPIKE_DEPS}/bgfx/src" -i "${ASTRO_SPIKE_DEPS}/bgfx/examples/common"
            COMMAND "${_astro_color_shaderc}" -f "${PROJECT_SOURCE_DIR}/editor/shaders/scene_textured.sc"
                -o "${_astro_color_shader_dir}/fs_scene_textured.bin" --type fragment
                --platform windows -p s_5_0 --varyingdef "${PROJECT_SOURCE_DIR}/editor/shaders/scene_textured.varying.def.sc"
                -i "${ASTRO_SPIKE_DEPS}/bgfx/src" -i "${ASTRO_SPIKE_DEPS}/bgfx/examples/common"
            COMMAND "${_astro_color_shaderc}" -f "${PROJECT_SOURCE_DIR}/editor/shaders/scene_lit_color.vs.sc"
                -o "${_astro_color_shader_dir}/vs_scene_lit_color.bin" --type vertex
                --platform windows -p s_5_0 --varyingdef "${PROJECT_SOURCE_DIR}/editor/shaders/scene_lit_color.varying.def.sc"
                -i "${ASTRO_SPIKE_DEPS}/bgfx/src"
            COMMAND "${_astro_color_shaderc}" -f "${PROJECT_SOURCE_DIR}/editor/shaders/scene_lit_color.fs.sc"
                -o "${_astro_color_shader_dir}/fs_scene_lit_color.bin" --type fragment
                --platform windows -p s_5_0 --varyingdef "${PROJECT_SOURCE_DIR}/editor/shaders/scene_lit_color.varying.def.sc"
                -i "${ASTRO_SPIKE_DEPS}/bgfx/src"
            COMMAND "${_astro_color_shaderc}" -f "${PROJECT_SOURCE_DIR}/editor/shaders/scene_lit_textured.vs.sc"
                -o "${_astro_color_shader_dir}/vs_scene_lit_textured.bin" --type vertex
                --platform windows -p s_5_0 --varyingdef "${PROJECT_SOURCE_DIR}/editor/shaders/scene_lit_textured.varying.def.sc"
                -i "${ASTRO_SPIKE_DEPS}/bgfx/src"
            COMMAND "${_astro_color_shaderc}" -f "${PROJECT_SOURCE_DIR}/editor/shaders/scene_lit_textured.fs.sc"
                -o "${_astro_color_shader_dir}/fs_scene_lit_textured.bin" --type fragment
                --platform windows -p s_5_0 --varyingdef "${PROJECT_SOURCE_DIR}/editor/shaders/scene_lit_textured.varying.def.sc"
                -i "${ASTRO_SPIKE_DEPS}/bgfx/src"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_astro_color_shader_source}/vs_cubes.bin"
                "${_astro_color_shader_dir}/vs_cubes.bin"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_astro_color_shader_source}/vs_stencil_texture.bin"
                "${_astro_color_shader_dir}/vs_stencil_texture.bin"
            DEPENDS
                "${PROJECT_SOURCE_DIR}/editor/shaders/scene_color.sc"
                "${PROJECT_SOURCE_DIR}/editor/shaders/scene_textured.sc"
                "${PROJECT_SOURCE_DIR}/editor/shaders/scene_lit_color.vs.sc"
                "${PROJECT_SOURCE_DIR}/editor/shaders/scene_lit_color.fs.sc"
                "${PROJECT_SOURCE_DIR}/editor/shaders/scene_lit_textured.vs.sc"
                "${PROJECT_SOURCE_DIR}/editor/shaders/scene_lit_textured.fs.sc"
                "${PROJECT_SOURCE_DIR}/editor/shaders/scene_color.varying.def.sc"
                "${PROJECT_SOURCE_DIR}/editor/shaders/scene_textured.varying.def.sc"
                "${PROJECT_SOURCE_DIR}/editor/shaders/scene_lit_color.varying.def.sc"
                "${PROJECT_SOURCE_DIR}/editor/shaders/scene_lit_textured.varying.def.sc"
                "${ASTRO_SPIKE_DEPS}/bgfx/src/bgfx_shader.sh"
                "${ASTRO_SPIKE_DEPS}/bgfx/examples/common/shaderlib.sh"
                "${_astro_color_shaderc}"
            VERBATIM)
        add_executable(astro_bgfx_color_space_smoke color_space_smoke.cpp)
        target_compile_features(astro_bgfx_color_space_smoke PRIVATE cxx_std_20)
        target_compile_options(astro_bgfx_color_space_smoke PRIVATE /W4 /permissive- /EHsc /Zc:__cplusplus /Zc:preprocessor /utf-8)
        target_compile_definitions(astro_bgfx_color_space_smoke PRIVATE
            BX_CONFIG_DEBUG=$<IF:$<CONFIG:Debug>,1,0>
            "ASTRO_COLOR_CONTRACT_SHADER_DIR=\"${_astro_color_shader_dir}\"")
        target_include_directories(astro_bgfx_color_space_smoke PRIVATE
            "${ASTRO_SPIKE_DEPS}/bgfx/include" "${ASTRO_SPIKE_DEPS}/bx/include")
        target_link_libraries(astro_bgfx_color_space_smoke PRIVATE astro_runtime_render3d spike_bgfx spike_bimg spike_bx
            SDL3::SDL3-static d3d11 dxgi dxguid)
        add_custom_target(astro_bgfx_color_contract_shaders DEPENDS ${_astro_color_shader_outputs})
        add_dependencies(astro_bgfx_color_space_smoke astro_bgfx_color_contract_shaders)
        set(_astro_color_contract_screenshot "${CMAKE_CURRENT_BINARY_DIR}/color-contract.tga")
        file(TO_NATIVE_PATH "${_astro_color_contract_screenshot}" _astro_color_contract_screenshot_native)
        add_test(NAME runtime.render3d.color_contract.bgfx.smoke
            COMMAND "${CMAKE_COMMAND}"
                "-DAPP=$<TARGET_FILE:astro_bgfx_color_space_smoke>"
                "-DSCREENSHOT=${_astro_color_contract_screenshot}"
                "-DSCREENSHOT_NATIVE=${_astro_color_contract_screenshot_native}"
                -P "${PROJECT_SOURCE_DIR}/tests/smoke/bgfx_color_contract.cmake")
        set_tests_properties(runtime.render3d.color_contract.bgfx.smoke PROPERTIES
            LABELS "smoke;graphics;3d;texture;color;lighting" TIMEOUT 45 RUN_SERIAL TRUE)
    endif()
    if(ASTRO_BUILD_RUNTIME_BGFX_3D)
        set(_runtime_packet_screenshot "${CMAKE_CURRENT_BINARY_DIR}/runtime-scene-packet.tga")
        file(TO_NATIVE_PATH "${_runtime_packet_screenshot}" _runtime_packet_screenshot_native)
        add_test(NAME runtime.render3d.scene_packet.bgfx.smoke
            COMMAND "${CMAKE_COMMAND}" -E env
                "ASTRO_RUNTIME_PACKET_SCREENSHOT=${_runtime_packet_screenshot_native}"
                "$<TARGET_FILE:astro_render3d_bgfx_smoke>" --runtime-packet-smoke)
        set_tests_properties(runtime.render3d.scene_packet.bgfx.smoke PROPERTIES
            LABELS "smoke;graphics;3d;runtime;scene" TIMEOUT 45 RUN_SERIAL TRUE)
        set_tests_properties(runtime.render3d.scene_packet.bgfx.smoke PROPERTIES
            PASS_REGULAR_EXPRESSION "runtime_packet_items=2 runtime_packet_diagnostics=0 runtime_items_submitted=2 runtime_gpu_mesh_cache=2 runtime_packet_valid=1 runtime_session_playing=1 runtime_scene_is_copy=1 runtime_fixed_steps=[1-9][0-9]* authoring_scene_unchanged=1 runtime_screenshot_saved=1")
    endif()
endif()

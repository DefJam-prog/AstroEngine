set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(imgui "${ASTRO_SPIKE_DEPS}/imgui")
add_executable(astro_editor main.cpp ProjectFile.cpp ProjectPathPolicy.cpp Localization.cpp AssetBrowserModel.cpp SceneViewRenderPolicy.cpp EditorUiScalePolicy.cpp EditorWorkspaceLayoutPolicy.cpp CSharpPlayApproval.cpp CSharpCodeWorkspace.cpp CSharpBuildManifest.cpp CSharpMetadataDiagnostics.cpp VulkanSurfaceFormatProbe.cpp)
target_compile_features(astro_editor PRIVATE cxx_std_20)
set_target_properties(astro_editor PROPERTIES CXX_EXTENSIONS OFF)
target_compile_options(astro_editor PRIVATE /W4 /EHsc /utf-8 /Zc:__cplusplus /Zc:preprocessor)
file(TO_CMAKE_PATH "${PROJECT_SOURCE_DIR}" _astro_editor_repository_root)
target_compile_definitions(astro_editor PRIVATE "ASTRO_EDITOR_REPOSITORY_ROOT=\"${_astro_editor_repository_root}\"")
target_compile_definitions(astro_editor PRIVATE BX_CONFIG_DEBUG=$<IF:$<CONFIG:Debug>,1,0>)
target_include_directories(astro_editor PRIVATE
    "${imgui}" "${imgui}/backends"
    "${ASTRO_SPIKE_DEPS}/bgfx/include" "${ASTRO_SPIKE_DEPS}/bgfx/3rdparty/khronos/vulkan-local"
    "${ASTRO_SPIKE_DEPS}/bx/include"
    "${ASTRO_SPIKE_DEPS}/bgfx/examples/common/imgui")
target_link_libraries(astro_editor PRIVATE astro_ai_editor astro_core astro_scene astro_scene_render2d astro_visual_logic astro_ui_editor astro_runtime_session astro_runtime_animation astro_runtime_camera astro_runtime_sdl_input astro_save_editor spike_imgui spike_bgfx spike_bimg spike_bx
    d3d11 dxgi dxguid psapi windowscodecs ole32)
if(TARGET astro_gltf)
    set(_astro_editor_3d_shader_source "${ASTRO_SPIKE_DEPS}/bgfx/examples/runtime/shaders/dxbc")
    foreach(_astro_editor_3d_shader IN ITEMS vs_cubes.bin vs_stencil_texture.bin)
        if(NOT EXISTS "${_astro_editor_3d_shader_source}/${_astro_editor_3d_shader}")
            message(FATAL_ERROR "Editor 3D Scene View requires cached bgfx shader ${_astro_editor_3d_shader}")
        endif()
    endforeach()
    set(_astro_shaderc "${ASTRO_SPIKE_DEPS}/bgfx/.build/win64_vs2022/bin/shadercRelease.exe")
    if(NOT EXISTS "${_astro_shaderc}")
        set(_astro_shaderc "${ASTRO_SPIKE_DEPS}/bgfx/.build/win64_vs2022/bin/shaderc.exe")
    endif()
    if(NOT EXISTS "${_astro_shaderc}")
        message(FATAL_ERROR "Editor alpha shaders need local BGFX shaderc. Generate BGFX tool projects with `genie --with-tools vs2022` in ${ASTRO_SPIKE_DEPS}/bgfx and build shaderc.vcxproj Release|x64.")
    endif()
    set(_astro_scene_shader_output_dir "${CMAKE_CURRENT_BINARY_DIR}/shaders")
    set(_astro_vulkan_shader_output_dir "${_astro_scene_shader_output_dir}/vulkan")
    set(_astro_scene_shader_outputs
        "${_astro_scene_shader_output_dir}/fs_scene_color.bin"
        "${_astro_scene_shader_output_dir}/fs_scene_textured.bin"
        "${_astro_scene_shader_output_dir}/vs_scene_lit_color.bin"
        "${_astro_scene_shader_output_dir}/fs_scene_lit_color.bin"
        "${_astro_scene_shader_output_dir}/vs_scene_lit_textured.bin"
        "${_astro_scene_shader_output_dir}/fs_scene_lit_textured.bin"
        "${_astro_vulkan_shader_output_dir}/fs_scene_color.bin"
        "${_astro_vulkan_shader_output_dir}/fs_scene_textured.bin"
        "${_astro_vulkan_shader_output_dir}/vs_scene_lit_color.bin"
        "${_astro_vulkan_shader_output_dir}/fs_scene_lit_color.bin"
        "${_astro_vulkan_shader_output_dir}/vs_scene_lit_textured.bin"
        "${_astro_vulkan_shader_output_dir}/fs_scene_lit_textured.bin"
        "${_astro_vulkan_shader_output_dir}/vs_ocornut_imgui.bin"
        "${_astro_vulkan_shader_output_dir}/fs_ocornut_imgui.bin"
        "${_astro_vulkan_shader_output_dir}/vs_cubes.bin"
        "${_astro_vulkan_shader_output_dir}/vs_stencil_texture.bin")
    add_custom_command(OUTPUT ${_astro_scene_shader_outputs}
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_astro_scene_shader_output_dir}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_astro_vulkan_shader_output_dir}"
        COMMAND "${_astro_shaderc}" -f "${CMAKE_CURRENT_SOURCE_DIR}/shaders/scene_color.sc"
            -o "${_astro_scene_shader_output_dir}/fs_scene_color.bin" --type fragment
            --platform windows -p s_5_0 --varyingdef "${CMAKE_CURRENT_SOURCE_DIR}/shaders/scene_color.varying.def.sc"
            -i "${ASTRO_SPIKE_DEPS}/bgfx/src" -i "${ASTRO_SPIKE_DEPS}/bgfx/examples/common"
        COMMAND "${_astro_shaderc}" -f "${CMAKE_CURRENT_SOURCE_DIR}/shaders/scene_textured.sc"
            -o "${_astro_scene_shader_output_dir}/fs_scene_textured.bin" --type fragment
            --platform windows -p s_5_0 --varyingdef "${CMAKE_CURRENT_SOURCE_DIR}/shaders/scene_textured.varying.def.sc"
            -i "${ASTRO_SPIKE_DEPS}/bgfx/src" -i "${ASTRO_SPIKE_DEPS}/bgfx/examples/common"
        COMMAND "${_astro_shaderc}" -f "${CMAKE_CURRENT_SOURCE_DIR}/shaders/scene_lit_color.vs.sc"
            -o "${_astro_scene_shader_output_dir}/vs_scene_lit_color.bin" --type vertex
            --platform windows -p s_5_0 --varyingdef "${CMAKE_CURRENT_SOURCE_DIR}/shaders/scene_lit_color.varying.def.sc"
            -i "${ASTRO_SPIKE_DEPS}/bgfx/src"
        COMMAND "${_astro_shaderc}" -f "${CMAKE_CURRENT_SOURCE_DIR}/shaders/scene_lit_color.fs.sc"
            -o "${_astro_scene_shader_output_dir}/fs_scene_lit_color.bin" --type fragment
            --platform windows -p s_5_0 --varyingdef "${CMAKE_CURRENT_SOURCE_DIR}/shaders/scene_lit_color.varying.def.sc"
            -i "${ASTRO_SPIKE_DEPS}/bgfx/src"
        COMMAND "${_astro_shaderc}" -f "${CMAKE_CURRENT_SOURCE_DIR}/shaders/scene_lit_textured.vs.sc"
            -o "${_astro_scene_shader_output_dir}/vs_scene_lit_textured.bin" --type vertex
            --platform windows -p s_5_0 --varyingdef "${CMAKE_CURRENT_SOURCE_DIR}/shaders/scene_lit_textured.varying.def.sc"
            -i "${ASTRO_SPIKE_DEPS}/bgfx/src"
        COMMAND "${_astro_shaderc}" -f "${CMAKE_CURRENT_SOURCE_DIR}/shaders/scene_lit_textured.fs.sc"
            -o "${_astro_scene_shader_output_dir}/fs_scene_lit_textured.bin" --type fragment
            --platform windows -p s_5_0 --varyingdef "${CMAKE_CURRENT_SOURCE_DIR}/shaders/scene_lit_textured.varying.def.sc"
            -i "${ASTRO_SPIKE_DEPS}/bgfx/src"
        COMMAND "${_astro_shaderc}" -f "${CMAKE_CURRENT_SOURCE_DIR}/shaders/scene_color.sc"
            -o "${_astro_vulkan_shader_output_dir}/fs_scene_color.bin" --type fragment
            --platform windows -p spirv --varyingdef "${CMAKE_CURRENT_SOURCE_DIR}/shaders/scene_color.varying.def.sc"
            -i "${ASTRO_SPIKE_DEPS}/bgfx/src" -i "${ASTRO_SPIKE_DEPS}/bgfx/examples/common"
        COMMAND "${_astro_shaderc}" -f "${CMAKE_CURRENT_SOURCE_DIR}/shaders/scene_textured.sc"
            -o "${_astro_vulkan_shader_output_dir}/fs_scene_textured.bin" --type fragment
            --platform windows -p spirv --varyingdef "${CMAKE_CURRENT_SOURCE_DIR}/shaders/scene_textured.varying.def.sc"
            -i "${ASTRO_SPIKE_DEPS}/bgfx/src" -i "${ASTRO_SPIKE_DEPS}/bgfx/examples/common"
        COMMAND "${_astro_shaderc}" -f "${CMAKE_CURRENT_SOURCE_DIR}/shaders/scene_lit_color.vs.sc"
            -o "${_astro_vulkan_shader_output_dir}/vs_scene_lit_color.bin" --type vertex
            --platform windows -p spirv --varyingdef "${CMAKE_CURRENT_SOURCE_DIR}/shaders/scene_lit_color.varying.def.sc"
            -i "${ASTRO_SPIKE_DEPS}/bgfx/src"
        COMMAND "${_astro_shaderc}" -f "${CMAKE_CURRENT_SOURCE_DIR}/shaders/scene_lit_color.fs.sc"
            -o "${_astro_vulkan_shader_output_dir}/fs_scene_lit_color.bin" --type fragment
            --platform windows -p spirv --varyingdef "${CMAKE_CURRENT_SOURCE_DIR}/shaders/scene_lit_color.varying.def.sc"
            -i "${ASTRO_SPIKE_DEPS}/bgfx/src"
        COMMAND "${_astro_shaderc}" -f "${CMAKE_CURRENT_SOURCE_DIR}/shaders/scene_lit_textured.vs.sc"
            -o "${_astro_vulkan_shader_output_dir}/vs_scene_lit_textured.bin" --type vertex
            --platform windows -p spirv --varyingdef "${CMAKE_CURRENT_SOURCE_DIR}/shaders/scene_lit_textured.varying.def.sc"
            -i "${ASTRO_SPIKE_DEPS}/bgfx/src"
        COMMAND "${_astro_shaderc}" -f "${CMAKE_CURRENT_SOURCE_DIR}/shaders/scene_lit_textured.fs.sc"
            -o "${_astro_vulkan_shader_output_dir}/fs_scene_lit_textured.bin" --type fragment
            --platform windows -p spirv --varyingdef "${CMAKE_CURRENT_SOURCE_DIR}/shaders/scene_lit_textured.varying.def.sc"
            -i "${ASTRO_SPIKE_DEPS}/bgfx/src"
        COMMAND "${_astro_shaderc}" -f "${ASTRO_SPIKE_DEPS}/bgfx/examples/common/imgui/vs_ocornut_imgui.sc"
            -o "${_astro_vulkan_shader_output_dir}/vs_ocornut_imgui.bin" --type vertex
            --platform windows -p spirv --varyingdef "${ASTRO_SPIKE_DEPS}/bgfx/examples/common/imgui/varying.def.sc"
            -i "${ASTRO_SPIKE_DEPS}/bgfx/src" -i "${ASTRO_SPIKE_DEPS}/bgfx/examples/common"
        COMMAND "${_astro_shaderc}" -f "${ASTRO_SPIKE_DEPS}/bgfx/examples/common/imgui/fs_ocornut_imgui.sc"
            -o "${_astro_vulkan_shader_output_dir}/fs_ocornut_imgui.bin" --type fragment
            --platform windows -p spirv --varyingdef "${ASTRO_SPIKE_DEPS}/bgfx/examples/common/imgui/varying.def.sc"
            -i "${ASTRO_SPIKE_DEPS}/bgfx/src" -i "${ASTRO_SPIKE_DEPS}/bgfx/examples/common"
        COMMAND "${_astro_shaderc}" -f "${ASTRO_SPIKE_DEPS}/bgfx/examples/01-cubes/vs_cubes.sc"
            -o "${_astro_vulkan_shader_output_dir}/vs_cubes.bin" --type vertex
            --platform windows -p spirv --varyingdef "${ASTRO_SPIKE_DEPS}/bgfx/examples/01-cubes/varying.def.sc"
            -i "${ASTRO_SPIKE_DEPS}/bgfx/src" -i "${ASTRO_SPIKE_DEPS}/bgfx/examples/common"
        COMMAND "${_astro_shaderc}" -f "${ASTRO_SPIKE_DEPS}/bgfx/examples/13-stencil/vs_stencil_texture.sc"
            -o "${_astro_vulkan_shader_output_dir}/vs_stencil_texture.bin" --type vertex
            --platform windows -p spirv --varyingdef "${ASTRO_SPIKE_DEPS}/bgfx/examples/13-stencil/varying.def.sc"
            -i "${ASTRO_SPIKE_DEPS}/bgfx/src" -i "${ASTRO_SPIKE_DEPS}/bgfx/examples/common"
        DEPENDS
            shaders/scene_color.sc shaders/scene_textured.sc
            shaders/scene_color.varying.def.sc shaders/scene_textured.varying.def.sc
            shaders/scene_lit_color.vs.sc shaders/scene_lit_color.fs.sc shaders/scene_lit_color.varying.def.sc
            shaders/scene_lit_textured.vs.sc shaders/scene_lit_textured.fs.sc shaders/scene_lit_textured.varying.def.sc
            "${ASTRO_SPIKE_DEPS}/bgfx/examples/common/imgui/vs_ocornut_imgui.sc"
            "${ASTRO_SPIKE_DEPS}/bgfx/examples/common/imgui/fs_ocornut_imgui.sc"
            "${ASTRO_SPIKE_DEPS}/bgfx/examples/common/imgui/varying.def.sc"
            "${ASTRO_SPIKE_DEPS}/bgfx/examples/01-cubes/vs_cubes.sc"
            "${ASTRO_SPIKE_DEPS}/bgfx/examples/01-cubes/varying.def.sc"
            "${ASTRO_SPIKE_DEPS}/bgfx/examples/13-stencil/vs_stencil_texture.sc"
            "${ASTRO_SPIKE_DEPS}/bgfx/examples/13-stencil/varying.def.sc"
            "${ASTRO_SPIKE_DEPS}/bgfx/src/bgfx_shader.sh"
            "${ASTRO_SPIKE_DEPS}/bgfx/examples/common/shaderlib.sh"
            "${_astro_shaderc}"
        VERBATIM)
    add_custom_target(astro_editor_scene_view_shaders DEPENDS ${_astro_scene_shader_outputs})
    add_dependencies(astro_editor astro_editor_scene_view_shaders)
    target_sources(astro_editor PRIVATE SceneViewCamera3D.cpp SceneCameraPose.cpp)
    target_link_libraries(astro_editor PRIVATE astro_gltf astro_runtime_render3d astro_scene_render3d astro_runtime_sdl_wic_image)
    target_compile_definitions(astro_editor PRIVATE ASTRO_EDITOR_HAS_RENDER3D_SCENE_VIEW=1
        "ASTRO_EDITOR_3D_SHADER_DIR=\"${CMAKE_CURRENT_BINARY_DIR}/shaders\""
        "ASTRO_EDITOR_RENDER3D_FIXTURE_DIR=\"${PROJECT_SOURCE_DIR}/tests/fixtures/render3d_scene_project\"")
    add_custom_command(TARGET astro_editor POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_astro_editor_3d_shader_source}/vs_cubes.bin" "${CMAKE_CURRENT_BINARY_DIR}/shaders/vs_cubes.bin"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_astro_editor_3d_shader_source}/fs_cubes.bin" "${CMAKE_CURRENT_BINARY_DIR}/shaders/fs_cubes.bin"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_astro_editor_3d_shader_source}/vs_stencil_texture.bin" "${CMAKE_CURRENT_BINARY_DIR}/shaders/vs_stencil_texture.bin"
        VERBATIM)
    if(BUILD_TESTING)
        add_test(NAME editor.bgfx.backend.default.smoke COMMAND astro_editor --smoke-bgfx-backend)
        set_tests_properties(editor.bgfx.backend.default.smoke PROPERTIES
            TIMEOUT 45 LABELS "smoke;editor;graphics;renderer" RUN_SERIAL TRUE
            WORKING_DIRECTORY "${PROJECT_BINARY_DIR}")
        add_test(NAME editor.bgfx.backend.vulkan.smoke COMMAND astro_editor --smoke-bgfx-vulkan)
        set_tests_properties(editor.bgfx.backend.vulkan.smoke PROPERTIES
            TIMEOUT 45 LABELS "smoke;editor;graphics;renderer;vulkan" RUN_SERIAL TRUE
            SKIP_RETURN_CODE 77 WORKING_DIRECTORY "${PROJECT_BINARY_DIR}")
        add_test(NAME editor.bgfx.vulkan.scene_view.material.smoke
            COMMAND "${CMAKE_COMMAND}" -DAPP=$<TARGET_FILE:astro_editor>
                -DWORK_DIR=${PROJECT_BINARY_DIR}/tests/bgfx-backend-material-compare
                -P "${PROJECT_SOURCE_DIR}/tests/smoke/editor_bgfx_backend_material_compare.cmake")
        set_tests_properties(editor.bgfx.vulkan.scene_view.material.smoke PROPERTIES
            TIMEOUT 90 LABELS "smoke;editor;graphics;render3d;material;vulkan" RUN_SERIAL TRUE
            SKIP_RETURN_CODE 77 WORKING_DIRECTORY "${PROJECT_BINARY_DIR}"
            SKIP_REGULAR_EXPRESSION "VULKAN_BACKEND_UNAVAILABLE"
            ENVIRONMENT "ASTRO_EDITOR_SCREENSHOT_OUTPUT=${PROJECT_BINARY_DIR}/tests/editor-vulkan-scene-view-material.tga")
    endif()
endif()
if(TARGET astro_runtime_bgfx_scene3d)
    target_link_libraries(astro_editor PRIVATE astro_runtime_bgfx_scene3d)
    target_compile_definitions(astro_editor PRIVATE ASTRO_EDITOR_HAS_RUNTIME_BGFX_3D=1)
endif()
if(ASTRO_DOTNET_ROOT)
    file(TO_CMAKE_PATH "${ASTRO_DOTNET_ROOT}" _astro_editor_dotnet_root)
    target_compile_definitions(astro_editor PRIVATE "ASTRO_EDITOR_DOTNET_ROOT=\"${_astro_editor_dotnet_root}\"")
endif()
if(ASTRO_BUILD_CSHARP_RUNTIME AND ASTRO_DOTNET_ROOT)
    add_subdirectory("${PROJECT_SOURCE_DIR}/tools/csharp/metadata-validator"
        "${CMAKE_BINARY_DIR}/tools/csharp/metadata-validator")
    file(TO_CMAKE_PATH "${CMAKE_BINARY_DIR}/tools/csharp/metadata-validator/publish/AstroEngine.CSharpMetadataValidator.dll"
        _astro_metadata_validator_dll)
    target_compile_definitions(astro_editor PRIVATE
        "ASTRO_EDITOR_METADATA_VALIDATOR_DLL=\"${_astro_metadata_validator_dll}\"")
    add_dependencies(astro_editor astro_csharp_metadata_validator)
endif()
if(TARGET astro_runtime_csharp)
    target_link_libraries(astro_editor PRIVATE astro_runtime_csharp)
    target_compile_definitions(astro_editor PRIVATE
        ASTRO_EDITOR_HAS_CSHARP_RUNTIME=1
    )
endif()
if(TARGET astro_runtime_puzzle)
    target_link_libraries(astro_editor PRIVATE astro_runtime_puzzle)
    target_compile_definitions(astro_editor PRIVATE ASTRO_EDITOR_HAS_PUZZLE_RUNTIME=1)
endif()
if(TARGET astro_runtime_puzzle_save)
    target_link_libraries(astro_editor PRIVATE astro_runtime_puzzle_save)
    target_compile_definitions(astro_editor PRIVATE ASTRO_EDITOR_HAS_PUZZLE_SAVE=1)
endif()
if(BUILD_TESTING)
    add_executable(astro_editor_asset_browser_tests
        ../tests/unit/editor/AssetBrowserModelTests.cpp AssetBrowserModel.cpp)
    target_link_libraries(astro_editor_asset_browser_tests PRIVATE astro_assets)
    target_include_directories(astro_editor_asset_browser_tests PRIVATE .)
    target_compile_definitions(astro_editor_asset_browser_tests PRIVATE
        "ASTRO_EDITOR_ASSET_BROWSER_FIXTURE_DIR=\"${PROJECT_SOURCE_DIR}/tests/fixtures/render3d_scene_project\"")
    target_compile_features(astro_editor_asset_browser_tests PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(astro_editor_asset_browser_tests PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8)
    else()
        target_compile_options(astro_editor_asset_browser_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME editor.asset_browser.model.unit COMMAND astro_editor_asset_browser_tests)
    set_tests_properties(editor.asset_browser.model.unit PROPERTIES LABELS "unit;editor;assets" TIMEOUT 15)

    add_executable(astro_editor_ui_scale_policy_tests
        ../tests/unit/editor/EditorUiScalePolicyTests.cpp EditorUiScalePolicy.cpp)
    target_link_libraries(astro_editor_ui_scale_policy_tests PRIVATE spike_imgui)
    target_include_directories(astro_editor_ui_scale_policy_tests PRIVATE .)
    target_compile_features(astro_editor_ui_scale_policy_tests PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(astro_editor_ui_scale_policy_tests PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8)
    else()
        target_compile_options(astro_editor_ui_scale_policy_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME editor.ui_scale_policy.unit COMMAND astro_editor_ui_scale_policy_tests)
    set_tests_properties(editor.ui_scale_policy.unit PROPERTIES LABELS "unit;editor;window" TIMEOUT 15)

    add_executable(astro_editor_workspace_layout_policy_tests
        ../tests/unit/editor/EditorWorkspaceLayoutPolicyTests.cpp EditorWorkspaceLayoutPolicy.cpp)
    target_include_directories(astro_editor_workspace_layout_policy_tests PRIVATE .)
    target_compile_features(astro_editor_workspace_layout_policy_tests PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(astro_editor_workspace_layout_policy_tests PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8)
    else()
        target_compile_options(astro_editor_workspace_layout_policy_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME editor.workspace_layout_policy.unit COMMAND astro_editor_workspace_layout_policy_tests)
    set_tests_properties(editor.workspace_layout_policy.unit PROPERTIES LABELS "unit;editor;window" TIMEOUT 15)

    if(TARGET astro_gltf)
        add_executable(astro_editor_scene_camera_pose_tests
            ../tests/unit/editor/SceneCameraPoseTests.cpp SceneCameraPose.cpp)
        target_link_libraries(astro_editor_scene_camera_pose_tests PRIVATE astro_scene astro_runtime_render3d astro_scene_render3d spike_bx)
        target_include_directories(astro_editor_scene_camera_pose_tests PRIVATE . "${ASTRO_SPIKE_DEPS}/bx/include")
        target_compile_definitions(astro_editor_scene_camera_pose_tests PRIVATE
            BX_CONFIG_DEBUG=$<IF:$<CONFIG:Debug>,1,0>)
        target_compile_features(astro_editor_scene_camera_pose_tests PRIVATE cxx_std_20)
        if(MSVC)
            target_compile_options(astro_editor_scene_camera_pose_tests PRIVATE /W4 /permissive- /Zc:__cplusplus /Zc:preprocessor /utf-8)
        else()
            target_compile_options(astro_editor_scene_camera_pose_tests PRIVATE -Wall -Wextra -Wpedantic)
        endif()
        add_test(NAME editor.scene_camera_pose.unit COMMAND astro_editor_scene_camera_pose_tests)
        set_tests_properties(editor.scene_camera_pose.unit PROPERTIES LABELS "unit;editor;camera;3d" TIMEOUT 15)

        add_test(NAME editor.scene_view3d.benchmark.smoke
            COMMAND "${CMAKE_COMMAND}" -DAPP=$<TARGET_FILE:astro_editor>
                -P "${PROJECT_SOURCE_DIR}/tests/smoke/editor_3d_benchmark.cmake")
        set_tests_properties(editor.scene_view3d.benchmark.smoke PROPERTIES
            TIMEOUT 180 LABELS "smoke;editor;performance;3d" RUN_SERIAL TRUE)

        add_test(NAME editor.primitive_material.smoke
            COMMAND "${CMAKE_COMMAND}" -DAPP=$<TARGET_FILE:astro_editor>
                -DWORK_DIR=${PROJECT_BINARY_DIR}/tests/editor-primitive-material
                -P "${PROJECT_SOURCE_DIR}/tests/smoke/editor_primitive_material.cmake")
        set_tests_properties(editor.primitive_material.smoke PROPERTIES
            TIMEOUT 60 LABELS "smoke;editor;graphics;render3d;material" RUN_SERIAL TRUE)
        if(TARGET astro_runtime_bgfx_scene3d)
            add_test(NAME editor.runtime_bgfx_game_view.smoke
                COMMAND "${CMAKE_COMMAND}"
                    "-DAPP=$<TARGET_FILE:astro_editor>"
                    "-DWORK_DIR=${PROJECT_BINARY_DIR}/tests/runtime-bgfx-game-view"
                    -P "${PROJECT_SOURCE_DIR}/tests/smoke/editor_runtime_bgfx_game_view.cmake")
            set_tests_properties(editor.runtime_bgfx_game_view.smoke PROPERTIES
                TIMEOUT 60 LABELS "smoke;editor;graphics;render3d;runtime" RUN_SERIAL TRUE)
            add_test(NAME editor.runtime_bgfx_vulkan_game_view.texture.smoke
                COMMAND "${CMAKE_COMMAND}"
                    "-DAPP=$<TARGET_FILE:astro_editor>"
                    "-DWORK_DIR=${PROJECT_BINARY_DIR}/tests/runtime-bgfx-vulkan-game-view"
                    "-DVULKAN=ON"
                    -P "${PROJECT_SOURCE_DIR}/tests/smoke/editor_runtime_bgfx_game_view_texture.cmake")
            set_tests_properties(editor.runtime_bgfx_vulkan_game_view.texture.smoke PROPERTIES
                TIMEOUT 90 LABELS "smoke;editor;graphics;render3d;runtime;vulkan" RUN_SERIAL TRUE
                SKIP_REGULAR_EXPRESSION "VULKAN_BACKEND_UNAVAILABLE;VULKAN_GAME_VIEW_COMPOSITION_UNVERIFIED")
            add_test(NAME editor.runtime_bgfx_vulkan.color_path.chart.smoke
                COMMAND "${CMAKE_COMMAND}"
                    "-DAPP=$<TARGET_FILE:astro_editor>"
                    "-DWORK_DIR=${PROJECT_BINARY_DIR}/tests/runtime-bgfx-vulkan-color-chart"
                    "-DVULKAN=ON"
                    "-DCOLOR_CHART=ON"
                    -P "${PROJECT_SOURCE_DIR}/tests/smoke/editor_runtime_bgfx_game_view_texture.cmake")
            set_tests_properties(editor.runtime_bgfx_vulkan.color_path.chart.smoke PROPERTIES
                TIMEOUT 90 LABELS "smoke;editor;graphics;render3d;runtime;vulkan;color" RUN_SERIAL TRUE
                SKIP_RETURN_CODE 77 SKIP_REGULAR_EXPRESSION "VULKAN_BACKEND_UNAVAILABLE"
                WORKING_DIRECTORY "${PROJECT_BINARY_DIR}")
            add_test(NAME editor.runtime_bgfx_vulkan_game_view.tap.smoke
                COMMAND "${CMAKE_COMMAND}"
                    "-DAPP=$<TARGET_FILE:astro_editor>"
                    "-DWORK_DIR=${PROJECT_BINARY_DIR}/tests/runtime-bgfx-vulkan-game-view-tap"
                    "-DVULKAN=ON"
                    "-DTAP=ON"
                    "-DTAP_SIZE=1100x720"
                    -P "${PROJECT_SOURCE_DIR}/tests/smoke/editor_runtime_bgfx_game_view_texture.cmake")
            set_tests_properties(editor.runtime_bgfx_vulkan_game_view.tap.smoke PROPERTIES
                TIMEOUT 90 LABELS "smoke;editor;graphics;render3d;runtime;vulkan;diagnostics" RUN_SERIAL TRUE
                SKIP_RETURN_CODE 77 SKIP_REGULAR_EXPRESSION "VULKAN_BACKEND_UNAVAILABLE"
                WORKING_DIRECTORY "${PROJECT_BINARY_DIR}")
            add_test(NAME editor.runtime_bgfx_vulkan_game_view.tap.1280x800.smoke
                COMMAND "${CMAKE_COMMAND}"
                    "-DAPP=$<TARGET_FILE:astro_editor>"
                    "-DWORK_DIR=${PROJECT_BINARY_DIR}/tests/runtime-bgfx-vulkan-game-view-tap-1280x800"
                    "-DVULKAN=ON"
                    "-DTAP=ON"
                    "-DTAP_SIZE=1280x800"
                    -P "${PROJECT_SOURCE_DIR}/tests/smoke/editor_runtime_bgfx_game_view_texture.cmake")
            set_tests_properties(editor.runtime_bgfx_vulkan_game_view.tap.1280x800.smoke PROPERTIES
                TIMEOUT 90 LABELS "smoke;editor;graphics;render3d;runtime;vulkan;diagnostics;window" RUN_SERIAL TRUE
                SKIP_RETURN_CODE 77 SKIP_REGULAR_EXPRESSION "VULKAN_BACKEND_UNAVAILABLE"
                WORKING_DIRECTORY "${PROJECT_BINARY_DIR}")
            add_test(NAME editor.runtime_bgfx_vulkan_game_view.tap.resize_stress.smoke
                COMMAND "${CMAKE_COMMAND}"
                    "-DAPP=$<TARGET_FILE:astro_editor>"
                    "-DWORK_DIR=${PROJECT_BINARY_DIR}/tests/runtime-bgfx-vulkan-game-view-tap-resize-stress"
                    "-DVULKAN=ON"
                    "-DTAP=ON"
                    "-DTAP_SIZE=1100x720"
                    "-DSTRESS_CYCLES=2"
                    -P "${PROJECT_SOURCE_DIR}/tests/smoke/editor_runtime_bgfx_game_view_texture.cmake")
            set_tests_properties(editor.runtime_bgfx_vulkan_game_view.tap.resize_stress.smoke PROPERTIES
                TIMEOUT 90 LABELS "smoke;editor;graphics;render3d;runtime;vulkan;diagnostics;resize" RUN_SERIAL TRUE
                SKIP_RETURN_CODE 77 SKIP_REGULAR_EXPRESSION "VULKAN_BACKEND_UNAVAILABLE"
                WORKING_DIRECTORY "${PROJECT_BINARY_DIR}")
          add_test(NAME editor.runtime_bgfx_directional_light.smoke
              COMMAND "${CMAKE_COMMAND}"
                  "-DAPP=$<TARGET_FILE:astro_editor>"
                  "-DWORK_DIR=${PROJECT_BINARY_DIR}/tests/runtime-bgfx-directional-light"
                  -P "${PROJECT_SOURCE_DIR}/tests/smoke/editor_runtime_bgfx_directional_light.cmake")
          set_tests_properties(editor.runtime_bgfx_directional_light.smoke PROPERTIES
              TIMEOUT 60 LABELS "smoke;editor;graphics;render3d;runtime;lighting" RUN_SERIAL TRUE)
          add_test(NAME editor.runtime_bgfx_point_light.smoke
              COMMAND "${CMAKE_COMMAND}"
                  "-DAPP=$<TARGET_FILE:astro_editor>"
                  "-DWORK_DIR=${PROJECT_BINARY_DIR}/tests/runtime-bgfx-point-light"
                  -P "${PROJECT_SOURCE_DIR}/tests/smoke/editor_runtime_bgfx_point_light.cmake")
          set_tests_properties(editor.runtime_bgfx_point_light.smoke PROPERTIES
              TIMEOUT 60 LABELS "smoke;editor;graphics;render3d;runtime;lighting" RUN_SERIAL TRUE)
          add_test(NAME editor.runtime_bgfx_game_view_fallback.smoke
              COMMAND "${CMAKE_COMMAND}"
                  "-DAPP=$<TARGET_FILE:astro_editor>"
                  "-DWORK_DIR=${PROJECT_BINARY_DIR}/tests/runtime-bgfx-game-view-fallback"
                  -P "${PROJECT_SOURCE_DIR}/tests/smoke/editor_runtime_bgfx_game_view_fallback.cmake")
          set_tests_properties(editor.runtime_bgfx_game_view_fallback.smoke PROPERTIES
              TIMEOUT 60 LABELS "smoke;editor;graphics;render3d;runtime;fallback" RUN_SERIAL TRUE)
          add_test(NAME editor.runtime_bgfx_game_view_texture.smoke
              COMMAND "${CMAKE_COMMAND}"
                  "-DAPP=$<TARGET_FILE:astro_editor>"
                  "-DWORK_DIR=${PROJECT_BINARY_DIR}/tests/runtime-bgfx-game-view-texture"
                  -P "${PROJECT_SOURCE_DIR}/tests/smoke/editor_runtime_bgfx_game_view_texture.cmake")
          set_tests_properties(editor.runtime_bgfx_game_view_texture.smoke PROPERTIES
              TIMEOUT 60 LABELS "smoke;editor;graphics;render3d;runtime;texture" RUN_SERIAL TRUE)
          add_test(NAME editor.runtime_bgfx_game_view_mask.smoke
              COMMAND "${CMAKE_COMMAND}"
                  "-DAPP=$<TARGET_FILE:astro_editor>"
                  "-DWORK_DIR=${PROJECT_BINARY_DIR}/tests/runtime-bgfx-game-view-mask"
                  "-DMASK=ON"
                  -P "${PROJECT_SOURCE_DIR}/tests/smoke/editor_runtime_bgfx_game_view_texture.cmake")
          set_tests_properties(editor.runtime_bgfx_game_view_mask.smoke PROPERTIES
              TIMEOUT 60 LABELS "smoke;editor;graphics;render3d;runtime;texture;alpha;mask" RUN_SERIAL TRUE)
          foreach(_mip_filter IN ITEMS 9984 9985 9986 9987)
              add_test(NAME "editor.runtime_bgfx_game_view_mipmap.${_mip_filter}.smoke"
                  COMMAND "${CMAKE_COMMAND}"
                      "-DAPP=$<TARGET_FILE:astro_editor>"
                      "-DWORK_DIR=${PROJECT_BINARY_DIR}/tests/runtime-bgfx-game-view-mipmap-${_mip_filter}"
                      "-DFILTER=${_mip_filter}"
                      -P "${PROJECT_SOURCE_DIR}/tests/smoke/editor_runtime_bgfx_game_view_texture.cmake")
              set_tests_properties("editor.runtime_bgfx_game_view_mipmap.${_mip_filter}.smoke" PROPERTIES
                  TIMEOUT 60 LABELS "smoke;editor;graphics;render3d;runtime;texture;mipmap" RUN_SERIAL TRUE)
          endforeach()
        endif()
    endif()

    add_executable(astro_editor_scene_view_camera_tests
        ../tests/unit/editor/SceneViewCamera3DTests.cpp SceneViewCamera3D.cpp)
    target_link_libraries(astro_editor_scene_view_camera_tests PRIVATE astro_runtime_render3d)
    target_include_directories(astro_editor_scene_view_camera_tests PRIVATE .)
    target_compile_features(astro_editor_scene_view_camera_tests PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(astro_editor_scene_view_camera_tests PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8)
    else()
        target_compile_options(astro_editor_scene_view_camera_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME editor.scene_view_camera3d.unit COMMAND astro_editor_scene_view_camera_tests)
    set_tests_properties(editor.scene_view_camera3d.unit PROPERTIES LABELS "unit;editor;camera;3d" TIMEOUT 15)

    add_executable(astro_editor_scene_view_render_policy_tests
        ../tests/unit/editor/SceneViewRenderPolicyTests.cpp SceneViewRenderPolicy.cpp)
    target_link_libraries(astro_editor_scene_view_render_policy_tests PRIVATE astro_assets astro_scene)
    target_include_directories(astro_editor_scene_view_render_policy_tests PRIVATE .)
    target_compile_features(astro_editor_scene_view_render_policy_tests PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(astro_editor_scene_view_render_policy_tests PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8)
    else()
        target_compile_options(astro_editor_scene_view_render_policy_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME editor.scene_view_render_policy.unit COMMAND astro_editor_scene_view_render_policy_tests)
    set_tests_properties(editor.scene_view_render_policy.unit PROPERTIES LABELS "unit;editor;render3d;alpha" TIMEOUT 15)

    add_test(NAME editor.smoke COMMAND astro_editor --smoke)
    set_tests_properties(editor.smoke PROPERTIES TIMEOUT 30 LABELS "smoke;graphics" RUN_SERIAL TRUE)

    add_test(NAME editor.ai_change_review.smoke COMMAND astro_editor --smoke-ai-change-review)
    set_tests_properties(editor.ai_change_review.smoke PROPERTIES
        TIMEOUT 30 LABELS "smoke;editor;ai;commands" RUN_SERIAL TRUE)

    add_test(NAME editor.ai_sprite_visibility_review.smoke
        COMMAND astro_editor --smoke-ai-sprite-visibility-review)
    set_tests_properties(editor.ai_sprite_visibility_review.smoke PROPERTIES
        TIMEOUT 30 LABELS "smoke;editor;ai;commands;scene" RUN_SERIAL TRUE)

    add_test(NAME editor.ai_primitive_creation_review.smoke
        COMMAND astro_editor --smoke-ai-primitive-creation-review)
    set_tests_properties(editor.ai_primitive_creation_review.smoke PROPERTIES
        TIMEOUT 30 LABELS "smoke;editor;ai;commands;scene;primitive" RUN_SERIAL TRUE)

    add_test(NAME editor.ai_camera_addition_review.smoke
        COMMAND astro_editor --smoke-ai-camera-addition-review)
    set_tests_properties(editor.ai_camera_addition_review.smoke PROPERTIES
        TIMEOUT 30 LABELS "smoke;editor;ai;commands;scene;camera" RUN_SERIAL TRUE)

    add_test(NAME editor.ai_camera_lens_review.smoke
        COMMAND astro_editor --smoke-ai-camera-lens-review)
    set_tests_properties(editor.ai_camera_lens_review.smoke PROPERTIES
        TIMEOUT 30 LABELS "smoke;editor;ai;commands;scene;camera" RUN_SERIAL TRUE)

    add_test(NAME editor.ai_camera_activation_review.smoke
        COMMAND astro_editor --smoke-ai-camera-activation-review)
    set_tests_properties(editor.ai_camera_activation_review.smoke PROPERTIES
        TIMEOUT 30 LABELS "smoke;editor;ai;commands;scene;camera" RUN_SERIAL TRUE)

    add_test(NAME editor.ai_camera_clear_review.smoke
        COMMAND astro_editor --smoke-ai-camera-clear-review)
    set_tests_properties(editor.ai_camera_clear_review.smoke PROPERTIES
        TIMEOUT 30 LABELS "smoke;editor;ai;commands;scene;camera" RUN_SERIAL TRUE)

    add_test(NAME editor.ai_transform_review.smoke
        COMMAND astro_editor --smoke-ai-transform-review)
    set_tests_properties(editor.ai_transform_review.smoke PROPERTIES
        TIMEOUT 30 LABELS "smoke;editor;ai;commands;scene;camera;transform" RUN_SERIAL TRUE)

    add_test(NAME editor.ai_reparent_review.smoke
        COMMAND astro_editor --smoke-ai-reparent-review)
    set_tests_properties(editor.ai_reparent_review.smoke PROPERTIES
        TIMEOUT 30 LABELS "smoke;editor;ai;commands;scene;camera;hierarchy" RUN_SERIAL TRUE)

    add_test(NAME editor.hierarchy_reparent_drag.smoke
        COMMAND astro_editor --smoke-hierarchy-reparent)
    set_tests_properties(editor.hierarchy_reparent_drag.smoke PROPERTIES
        TIMEOUT 30 LABELS "smoke;editor;commands;scene;hierarchy;input" RUN_SERIAL TRUE)

    add_test(NAME editor.hierarchy_reparent_many_drag.smoke
        COMMAND astro_editor --smoke-hierarchy-reparent-many)
    set_tests_properties(editor.hierarchy_reparent_many_drag.smoke PROPERTIES
        TIMEOUT 30 LABELS "smoke;editor;commands;scene;hierarchy;input" RUN_SERIAL TRUE)

    add_test(NAME editor.hierarchy_multi_transform_inspector.smoke
        COMMAND astro_editor --smoke-hierarchy-multi-transform)
    set_tests_properties(editor.hierarchy_multi_transform_inspector.smoke PROPERTIES
        TIMEOUT 30 LABELS "smoke;editor;commands;scene;hierarchy;inspector;input" RUN_SERIAL TRUE)

    if(TARGET astro_gltf)
        add_test(NAME editor.game_view_camera_no_active.smoke
            COMMAND astro_editor --smoke-game-camera-no-active)
        set_tests_properties(editor.game_view_camera_no_active.smoke PROPERTIES
            TIMEOUT 30 LABELS "smoke;editor;camera;render3d" RUN_SERIAL TRUE)
        add_test(NAME editor.game_view_camera_invalid.smoke
            COMMAND astro_editor --smoke-game-camera-invalid)
        set_tests_properties(editor.game_view_camera_invalid.smoke PROPERTIES
            TIMEOUT 30 LABELS "smoke;editor;camera;render3d" RUN_SERIAL TRUE)
        add_test(NAME editor.game_view_mesh_diagnostic_entity.smoke
            COMMAND astro_editor --smoke-game-diagnostic-entity)
        set_tests_properties(editor.game_view_mesh_diagnostic_entity.smoke PROPERTIES
            TIMEOUT 30 LABELS "smoke;editor;render3d;diagnostics" RUN_SERIAL TRUE)
        add_test(NAME editor.game_view_registered_asset_diagnostic.smoke
            COMMAND astro_editor --smoke-game-diagnostic-asset)
        set_tests_properties(editor.game_view_registered_asset_diagnostic.smoke PROPERTIES
            TIMEOUT 30 LABELS "smoke;editor;render3d;diagnostics;assets" RUN_SERIAL TRUE)
    endif()

    add_test(NAME editor.asset_browser.smoke COMMAND astro_editor --smoke-asset-browser)
    set_tests_properties(editor.asset_browser.smoke PROPERTIES
        TIMEOUT 30 LABELS "smoke;editor;assets;window" RUN_SERIAL TRUE)
    if(TARGET astro_gltf)
        add_test(NAME editor.asset_model_preview.smoke COMMAND astro_editor --smoke-asset-model-preview)
        set_tests_properties(editor.asset_model_preview.smoke PROPERTIES
            TIMEOUT 30 LABELS "smoke;editor;assets;render3d" RUN_SERIAL TRUE)
    endif()

    add_test(NAME editor.window_geometry.sdl.smoke COMMAND astro_editor --smoke-window-geometry)
    set_tests_properties(editor.window_geometry.sdl.smoke PROPERTIES
        TIMEOUT 30 LABELS "smoke;editor;window;SDL3" RUN_SERIAL TRUE)

    add_test(NAME editor.window_mixed_dpi.sdl.smoke COMMAND astro_editor --smoke-window-mixed-dpi)
    set_tests_properties(editor.window_mixed_dpi.sdl.smoke PROPERTIES
        TIMEOUT 30 LABELS "smoke;editor;window;SDL3;dpi" RUN_SERIAL TRUE SKIP_RETURN_CODE 77)

    foreach(_alpha_case IN ITEMS below equal above)
        add_test(NAME editor.scene_view_mask_cutoff.${_alpha_case}.smoke
            COMMAND astro_editor --smoke-mask-${_alpha_case})
        set_tests_properties(editor.scene_view_mask_cutoff.${_alpha_case}.smoke PROPERTIES
            TIMEOUT 30 LABELS "smoke;graphics;render3d;alpha;mask" RUN_SERIAL TRUE)
    endforeach()
    foreach(_mip_case IN ITEMS near far)
        add_test(NAME editor.scene_view_mask_mips.${_mip_case}.smoke
            COMMAND astro_editor --smoke-mask-mips-${_mip_case})
        set_tests_properties(editor.scene_view_mask_mips.${_mip_case}.smoke PROPERTIES
            TIMEOUT 30 LABELS "smoke;graphics;render3d;alpha;mask;texture" RUN_SERIAL TRUE)
    endforeach()
    foreach(_pattern IN ITEMS checker hole)
        add_test(NAME editor.scene_view_mask_mips.${_pattern}.smoke
            COMMAND astro_editor --smoke-mask-mips-${_pattern})
        set_tests_properties(editor.scene_view_mask_mips.${_pattern}.smoke PROPERTIES
            TIMEOUT 30 LABELS "smoke;graphics;render3d;alpha;mask;texture" RUN_SERIAL TRUE)
    endforeach()
    foreach(_cutoff IN ITEMS 25 75)
        foreach(_partial_case IN ITEMS near far)
            add_test(NAME editor.scene_view_mask_mips.partial.cutoff${_cutoff}.${_partial_case}.smoke
                COMMAND astro_editor --smoke-mask-mips-partial-${_partial_case}-${_cutoff})
            set_tests_properties(editor.scene_view_mask_mips.partial.cutoff${_cutoff}.${_partial_case}.smoke PROPERTIES
                TIMEOUT 30 LABELS "smoke;graphics;render3d;alpha;mask;texture" RUN_SERIAL TRUE)
        endforeach()
    endforeach()
    foreach(_boundary IN ITEMS below first-pass above)
        foreach(_scale IN ITEMS near far)
            add_test(NAME editor.scene_view_mask_mips.boundary.${_boundary}.${_scale}.smoke
                COMMAND astro_editor --smoke-mask-mips-boundary-${_boundary}-${_scale})
            set_tests_properties(editor.scene_view_mask_mips.boundary.${_boundary}.${_scale}.smoke PROPERTIES
                TIMEOUT 30 LABELS "smoke;graphics;render3d;alpha;mask;texture" RUN_SERIAL TRUE)
        endforeach()
    endforeach()
    foreach(_scale IN ITEMS near far)
        add_test(NAME editor.scene_view_mask_mips.mixed_boundary.${_scale}.smoke
            COMMAND astro_editor --smoke-mask-mips-mixed-${_scale})
        set_tests_properties(editor.scene_view_mask_mips.mixed_boundary.${_scale}.smoke PROPERTIES
            TIMEOUT 30 LABELS "smoke;graphics;render3d;alpha;mask;texture" RUN_SERIAL TRUE)
    endforeach()
    foreach(_scale IN ITEMS near far)
        add_test(NAME editor.scene_view_mask_mips.cutoff_transition.${_scale}.smoke
            COMMAND astro_editor --smoke-mask-mips-transition-${_scale})
        set_tests_properties(editor.scene_view_mask_mips.cutoff_transition.${_scale}.smoke PROPERTIES
            TIMEOUT 30 LABELS "smoke;graphics;render3d;alpha;mask;texture" RUN_SERIAL TRUE)
    endforeach()
    foreach(_profile IN ITEMS compact hidpi)
        foreach(_scale IN ITEMS near far)
            add_test(NAME editor.scene_view_mask_mips.cutoff_transition.${_profile}.${_scale}.smoke
                COMMAND astro_editor --smoke-mask-mips-transition-${_profile}-${_scale})
            set_tests_properties(editor.scene_view_mask_mips.cutoff_transition.${_profile}.${_scale}.smoke PROPERTIES
                TIMEOUT 30 LABELS "smoke;graphics;render3d;alpha;mask;texture;window" RUN_SERIAL TRUE)
        endforeach()
    endforeach()
    add_executable(astro_sdl_input_adapter_tests ../tests/unit/runtime/SDLInputAdapterTests.cpp)
    target_link_libraries(astro_sdl_input_adapter_tests PRIVATE astro_runtime_sdl_input)
    target_compile_features(astro_sdl_input_adapter_tests PRIVATE cxx_std_20)
    target_compile_options(astro_sdl_input_adapter_tests PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8)
    add_test(NAME input.sdl.adapter.smoke COMMAND astro_sdl_input_adapter_tests)
    set_tests_properties(input.sdl.adapter.smoke PROPERTIES TIMEOUT 15 LABELS "smoke;input;SDL3")
endif()

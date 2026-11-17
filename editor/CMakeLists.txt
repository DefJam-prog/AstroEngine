set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(imgui "${ASTRO_SPIKE_DEPS}/imgui")
add_executable(astro_editor main.cpp ProjectFile.cpp Localization.cpp CSharpPlayApproval.cpp CSharpCodeWorkspace.cpp CSharpBuildManifest.cpp CSharpMetadataDiagnostics.cpp)
target_compile_features(astro_editor PRIVATE cxx_std_20)
set_target_properties(astro_editor PROPERTIES CXX_EXTENSIONS OFF)
target_compile_options(astro_editor PRIVATE /W4 /utf-8 /Zc:__cplusplus /Zc:preprocessor)
file(TO_CMAKE_PATH "${PROJECT_SOURCE_DIR}" _astro_editor_repository_root)
target_compile_definitions(astro_editor PRIVATE "ASTRO_EDITOR_REPOSITORY_ROOT=\"${_astro_editor_repository_root}\"")
target_compile_definitions(astro_editor PRIVATE BX_CONFIG_DEBUG=$<IF:$<CONFIG:Debug>,1,0>)
target_include_directories(astro_editor PRIVATE
    "${imgui}" "${imgui}/backends"
    "${ASTRO_SPIKE_DEPS}/bgfx/include" "${ASTRO_SPIKE_DEPS}/bx/include"
    "${ASTRO_SPIKE_DEPS}/bgfx/examples/common/imgui")
target_link_libraries(astro_editor PRIVATE astro_core astro_scene astro_scene_render2d astro_visual_logic astro_ui_editor astro_runtime_session astro_runtime_animation astro_runtime_camera astro_runtime_sdl_input astro_save_editor spike_imgui spike_bgfx spike_bimg spike_bx
    d3d11 dxgi dxguid psapi windowscodecs ole32)
if(TARGET astro_gltf)
    set(_astro_editor_3d_shader_source "${ASTRO_SPIKE_DEPS}/bgfx/examples/runtime/shaders/dxbc")
    foreach(_astro_editor_3d_shader IN ITEMS vs_cubes.bin fs_cubes.bin)
        if(NOT EXISTS "${_astro_editor_3d_shader_source}/${_astro_editor_3d_shader}")
            message(FATAL_ERROR "Editor 3D Scene View requires cached bgfx shader ${_astro_editor_3d_shader}")
        endif()
    endforeach()
    target_sources(astro_editor PRIVATE SceneViewCamera3D.cpp)
    target_link_libraries(astro_editor PRIVATE astro_gltf astro_runtime_render3d)
    target_compile_definitions(astro_editor PRIVATE ASTRO_EDITOR_HAS_RENDER3D_SCENE_VIEW=1
        "ASTRO_EDITOR_3D_SHADER_DIR=\"${CMAKE_CURRENT_BINARY_DIR}/shaders\""
        "ASTRO_EDITOR_RENDER3D_FIXTURE_DIR=\"${PROJECT_SOURCE_DIR}/tests/fixtures/render3d_scene_project\"")
    add_custom_command(TARGET astro_editor POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_astro_editor_3d_shader_source}/vs_cubes.bin" "${CMAKE_CURRENT_BINARY_DIR}/shaders/vs_cubes.bin"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_astro_editor_3d_shader_source}/fs_cubes.bin" "${CMAKE_CURRENT_BINARY_DIR}/shaders/fs_cubes.bin"
        VERBATIM)
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
    if(TARGET astro_gltf)
        add_test(NAME editor.scene_view3d.benchmark.smoke
            COMMAND "${CMAKE_COMMAND}" -DAPP=$<TARGET_FILE:astro_editor>
                -P "${PROJECT_SOURCE_DIR}/tests/smoke/editor_3d_benchmark.cmake")
        set_tests_properties(editor.scene_view3d.benchmark.smoke PROPERTIES
            TIMEOUT 180 LABELS "smoke;editor;performance;3d" RUN_SERIAL TRUE)
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

    add_test(NAME editor.smoke COMMAND astro_editor --smoke)
    set_tests_properties(editor.smoke PROPERTIES TIMEOUT 30 LABELS "smoke;graphics" RUN_SERIAL TRUE)

    add_executable(astro_sdl_input_adapter_tests ../tests/unit/runtime/SDLInputAdapterTests.cpp)
    target_link_libraries(astro_sdl_input_adapter_tests PRIVATE astro_runtime_sdl_input)
    target_compile_features(astro_sdl_input_adapter_tests PRIVATE cxx_std_20)
    target_compile_options(astro_sdl_input_adapter_tests PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8)
    add_test(NAME input.sdl.adapter.smoke COMMAND astro_sdl_input_adapter_tests)
    set_tests_properties(input.sdl.adapter.smoke PROPERTIES TIMEOUT 15 LABELS "smoke;input;SDL3")
endif()

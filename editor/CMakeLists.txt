set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(imgui "${ASTRO_SPIKE_DEPS}/imgui")
add_library(astro_runtime_sdl_input ../engine/runtime/adapters/sdl3/SDLInputAdapter.cpp)
target_include_directories(astro_runtime_sdl_input PUBLIC ../engine/runtime/include)
target_link_libraries(astro_runtime_sdl_input PUBLIC astro_runtime_input SDL3::SDL3-static)
target_compile_features(astro_runtime_sdl_input PUBLIC cxx_std_20)
set_target_properties(astro_runtime_sdl_input PROPERTIES CXX_EXTENSIONS OFF)
target_compile_options(astro_runtime_sdl_input PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8)

add_executable(astro_editor main.cpp ProjectFile.cpp)
target_compile_features(astro_editor PRIVATE cxx_std_20)
set_target_properties(astro_editor PROPERTIES CXX_EXTENSIONS OFF)
target_compile_options(astro_editor PRIVATE /W4 /utf-8 /Zc:__cplusplus /Zc:preprocessor)
target_compile_definitions(astro_editor PRIVATE BX_CONFIG_DEBUG=$<IF:$<CONFIG:Debug>,1,0>)
target_include_directories(astro_editor PRIVATE
    "${imgui}" "${imgui}/backends"
    "${ASTRO_SPIKE_DEPS}/bgfx/include" "${ASTRO_SPIKE_DEPS}/bx/include"
    "${ASTRO_SPIKE_DEPS}/bgfx/examples/common/imgui")
target_link_libraries(astro_editor PRIVATE astro_core astro_scene astro_visual_logic astro_ui_editor astro_runtime_sdl_input spike_imgui spike_bgfx spike_bimg spike_bx
    d3d11 dxgi dxguid psapi windowscodecs ole32)
if(BUILD_TESTING)
    add_test(NAME editor.smoke COMMAND astro_editor --smoke)
    set_tests_properties(editor.smoke PROPERTIES TIMEOUT 30 LABELS "smoke;graphics" RUN_SERIAL TRUE)

    add_executable(astro_sdl_input_adapter_tests ../tests/unit/runtime/SDLInputAdapterTests.cpp)
    target_link_libraries(astro_sdl_input_adapter_tests PRIVATE astro_runtime_sdl_input)
    target_compile_features(astro_sdl_input_adapter_tests PRIVATE cxx_std_20)
    target_compile_options(astro_sdl_input_adapter_tests PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8)
    add_test(NAME input.sdl.adapter.smoke COMMAND astro_sdl_input_adapter_tests)
    set_tests_properties(input.sdl.adapter.smoke PROPERTIES TIMEOUT 15 LABELS "smoke;input;SDL3")
endif()

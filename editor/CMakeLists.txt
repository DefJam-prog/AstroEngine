set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(imgui "${ASTRO_SPIKE_DEPS}/imgui")
add_executable(astro_editor main.cpp ProjectFile.cpp)
target_compile_features(astro_editor PRIVATE cxx_std_20)
set_target_properties(astro_editor PROPERTIES CXX_EXTENSIONS OFF)
target_compile_options(astro_editor PRIVATE /W4 /utf-8 /Zc:__cplusplus /Zc:preprocessor)
target_compile_definitions(astro_editor PRIVATE BX_CONFIG_DEBUG=$<IF:$<CONFIG:Debug>,1,0>)
target_include_directories(astro_editor PRIVATE
    "${imgui}" "${imgui}/backends"
    "${ASTRO_SPIKE_DEPS}/bgfx/include" "${ASTRO_SPIKE_DEPS}/bx/include"
    "${ASTRO_SPIKE_DEPS}/bgfx/examples/common/imgui")
target_link_libraries(astro_editor PRIVATE astro_core astro_scene spike_imgui spike_bgfx spike_bimg spike_bx
    d3d11 dxgi dxguid psapi)
if(BUILD_TESTING)
    add_test(NAME editor.smoke COMMAND astro_editor --smoke)
    set_tests_properties(editor.smoke PROPERTIES TIMEOUT 30 LABELS "smoke;graphics" RUN_SERIAL TRUE)
endif()

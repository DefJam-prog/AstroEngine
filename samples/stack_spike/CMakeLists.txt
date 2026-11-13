if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "The 00.3 experiment currently requires Windows x64 MSVC")
endif()
set(ASTRO_SPIKE_DEPS "${PROJECT_SOURCE_DIR}/build/spike-deps" CACHE PATH "Local experiment dependency cache")
foreach(required IN ITEMS SDL/CMakeLists.txt imgui/imgui.cpp bgfx/include/bgfx/bgfx.h bx/include/bx/math.h)
    if(NOT EXISTS "${ASTRO_SPIKE_DEPS}/${required}")
        message(FATAL_ERROR "Missing ${required}; prepare dependencies explicitly; configuration never downloads them")
    endif()
endforeach()
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory("${ASTRO_SPIKE_DEPS}/SDL" "${CMAKE_CURRENT_BINARY_DIR}/SDL" EXCLUDE_FROM_ALL)

foreach(lib IN ITEMS bgfx bimg bx)
    add_library(spike_${lib} STATIC IMPORTED GLOBAL)
    set_target_properties(spike_${lib} PROPERTIES
        IMPORTED_CONFIGURATIONS "DEBUG;RELEASE"
        IMPORTED_LOCATION_DEBUG "${ASTRO_SPIKE_DEPS}/bgfx/.build/win64_vs2022/bin/${lib}Debug.lib"
        IMPORTED_LOCATION_RELEASE "${ASTRO_SPIKE_DEPS}/bgfx/.build/win64_vs2022/bin/${lib}Release.lib")
endforeach()
set(imgui "${ASTRO_SPIKE_DEPS}/imgui")
add_library(spike_imgui STATIC
    "${imgui}/imgui.cpp" "${imgui}/imgui_draw.cpp"
    "${imgui}/imgui_tables.cpp" "${imgui}/imgui_widgets.cpp"
    "${imgui}/backends/imgui_impl_sdl3.cpp")
target_include_directories(spike_imgui PUBLIC "${imgui}" "${imgui}/backends")
target_link_libraries(spike_imgui PUBLIC SDL3::SDL3-static)

if(ASTRO_BUILD_STACK_SPIKE)
add_executable(astro_stack_spike main.cpp)
target_compile_features(astro_stack_spike PRIVATE cxx_std_20)
set_target_properties(astro_stack_spike PROPERTIES CXX_EXTENSIONS OFF)
target_compile_options(astro_stack_spike PRIVATE /W4 /utf-8 /Zc:__cplusplus /Zc:preprocessor)
target_compile_definitions(astro_stack_spike PRIVATE BX_CONFIG_DEBUG=$<IF:$<CONFIG:Debug>,1,0>)
target_include_directories(astro_stack_spike PRIVATE
    "${imgui}" "${imgui}/backends"
    "${ASTRO_SPIKE_DEPS}/bgfx/include" "${ASTRO_SPIKE_DEPS}/bx/include"
    "${ASTRO_SPIKE_DEPS}/bgfx/examples/common/imgui")
target_link_libraries(astro_stack_spike PRIVATE spike_imgui spike_bgfx spike_bimg spike_bx
    d3d11 dxgi dxguid psapi)
if(BUILD_TESTING)
    add_test(NAME stack_spike.smoke COMMAND astro_stack_spike --smoke)
    set_tests_properties(stack_spike.smoke PROPERTIES TIMEOUT 30 LABELS "smoke;graphics" RUN_SERIAL TRUE)
endif()

endif()

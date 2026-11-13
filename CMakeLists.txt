cmake_minimum_required(VERSION 3.20)

project(AstroEngine LANGUAGES CXX)

if(MSVC)
    # Match the locally prepared static SDL3/bgfx libraries used by the optional editor.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "Use an out-of-source build: cmake -S . -B build/debug")
endif()

include(CTest)
option(ASTRO_BUILD_PUZZLE_RUNTIME "Build the optional backend-neutral puzzle/animation runtime module" OFF)
option(ASTRO_BUILD_SAVE_RUNTIME "Build the optional selective game save runtime module" OFF)
option(ASTRO_ENABLE_CLANG_TIDY "Run clang-tidy during compilation" OFF)
if(ASTRO_ENABLE_CLANG_TIDY)
    find_program(ASTRO_CLANG_TIDY NAMES clang-tidy REQUIRED)
    set(CMAKE_CXX_CLANG_TIDY "${ASTRO_CLANG_TIDY}")
endif()
add_subdirectory(engine/runtime)
add_subdirectory(engine/core)
add_subdirectory(engine/assets)
add_subdirectory(engine/scene)
add_subdirectory(engine/prefabs)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

option(ASTRO_BUILD_STACK_SPIKE "Build the isolated Windows SDL3/bgfx/ImGui experiment" OFF)
option(ASTRO_BUILD_EDITOR "Build Windows editor" OFF)
option(ASTRO_BUILD_GAME_UI_DEMO "Build optional SDL3/ImGui runtime Game UI demo" OFF)
if(ASTRO_BUILD_STACK_SPIKE OR ASTRO_BUILD_EDITOR OR ASTRO_BUILD_GAME_UI_DEMO)
    add_subdirectory(samples/stack_spike)
endif()

if(ASTRO_BUILD_GAME_UI_DEMO)
    add_subdirectory(samples/game_ui_demo)
endif()

if(ASTRO_BUILD_EDITOR)
    add_subdirectory(editor)
endif()

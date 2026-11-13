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
option(ASTRO_ENABLE_CLANG_TIDY "Run clang-tidy during compilation" OFF)
if(ASTRO_ENABLE_CLANG_TIDY)
    find_program(ASTRO_CLANG_TIDY NAMES clang-tidy REQUIRED)
    set(CMAKE_CXX_CLANG_TIDY "${ASTRO_CLANG_TIDY}")
endif()
add_subdirectory(engine/runtime)
add_subdirectory(engine/core)
add_subdirectory(engine/assets)
add_subdirectory(engine/scene)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

option(ASTRO_BUILD_STACK_SPIKE "Build the isolated Windows SDL3/bgfx/ImGui experiment" OFF)
option(ASTRO_BUILD_EDITOR "Build Windows editor" OFF)
if(ASTRO_BUILD_STACK_SPIKE OR ASTRO_BUILD_EDITOR)
    add_subdirectory(samples/stack_spike)
endif()

if(ASTRO_BUILD_EDITOR)
    add_subdirectory(editor)
endif()

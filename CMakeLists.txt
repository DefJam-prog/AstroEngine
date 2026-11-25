cmake_minimum_required(VERSION 3.20)

project(AstroEngine LANGUAGES CXX)

# MSVC /showIncludes is localized on some Windows installations. CMake may
# mis-decode the emitted prefix, which disables Ninja header dependency scans.
# Allow the active console prefix to be supplied for localized toolchains.
set(ASTRO_MSVC_SHOWINCLUDES_PREFIX "" CACHE STRING
    "Override CMake's MSVC /showIncludes prefix when compiler output is localized")
if(MSVC AND ASTRO_MSVC_SHOWINCLUDES_PREFIX)
    set(CMAKE_CL_SHOWINCLUDES_PREFIX "${ASTRO_MSVC_SHOWINCLUDES_PREFIX}")
endif()

if(MSVC)
    # Match the locally prepared static SDL3/bgfx libraries used by the optional editor.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "Use an out-of-source build: cmake -S . -B build/debug")
endif()

include(CTest)
option(ASTRO_BUILD_PUZZLE_RUNTIME "Build the optional backend-neutral puzzle runtime module" OFF)
option(ASTRO_BUILD_ANIMATION_RUNTIME "Build the optional backend-neutral animation and timeline runtime module" OFF)
option(ASTRO_BUILD_SAVE_RUNTIME "Build the optional selective game save runtime module" OFF)
option(ASTRO_BUILD_CHECKPOINT_RUNTIME "Build the optional runtime checkpoint and respawn module" OFF)
option(ASTRO_BUILD_RENDER2D "Build the optional backend-neutral 2D render-command module" OFF)
option(ASTRO_BUILD_RENDER3D "Build the optional backend-neutral 3D camera and mesh module" OFF)
option(ASTRO_BUILD_GLTF_IMPORTER "Build the optional bounded glTF/GLB mesh importer" OFF)
set(ASTRO_GLTF_DEPS_ROOT "${CMAKE_SOURCE_DIR}/build/07.1b-deps" CACHE PATH
    "Ignored local source cache for the pinned glTF importer dependencies")
option(ASTRO_BUILD_RENDER3D_BGFX_SMOKE "Build optional Windows bgfx 3D visual smoke previews" OFF)
option(ASTRO_BUILD_RUNTIME_BGFX_3D "Build optional Windows BGFX consumer for Scene render packets" OFF)
option(ASTRO_BUILD_AI_EDITOR_API "Build the optional in-process AI/Codex Editor API adapter" OFF)
option(ASTRO_BUILD_2D_PLAYGROUND "Build the optional SDL3 2D Play/render playground" OFF)
option(ASTRO_BUILD_VISUAL_LOGIC "Build the optional typed Visual Logic graph runtime" OFF)
option(ASTRO_BUILD_STACK_SPIKE "Build the isolated Windows SDL3/bgfx/ImGui experiment" OFF)
option(ASTRO_BUILD_EDITOR "Build Windows editor" OFF)
option(ASTRO_BUILD_GAME_UI_DEMO "Build optional SDL3/ImGui runtime Game UI demo" OFF)
option(ASTRO_BUILD_CSHARP_RUNTIME "Build the experimental Windows C# scripting host" OFF)
set(ASTRO_DOTNET_ROOT "" CACHE PATH "Local dotnet SDK/runtime root for the experimental C# host")
option(ASTRO_ENABLE_CLANG_TIDY "Run clang-tidy during compilation" OFF)
if(ASTRO_ENABLE_CLANG_TIDY)
    find_program(ASTRO_CLANG_TIDY NAMES clang-tidy REQUIRED)
    set(CMAKE_CXX_CLANG_TIDY "${ASTRO_CLANG_TIDY}")
endif()
add_subdirectory(engine/runtime)
add_subdirectory(engine/core)
add_subdirectory(engine/assets)
add_subdirectory(engine/scene)
if(ASTRO_BUILD_AI_EDITOR_API OR BUILD_TESTING OR ASTRO_BUILD_EDITOR)
    add_subdirectory(engine/ai)
endif()
add_subdirectory(engine/prefabs)

if(ASTRO_BUILD_EDITOR OR BUILD_TESTING)
    add_subdirectory(engine/save_editor)
endif()

if(ASTRO_BUILD_RENDER3D OR ASTRO_BUILD_GLTF_IMPORTER OR ASTRO_BUILD_RENDER3D_BGFX_SMOKE OR ASTRO_BUILD_RUNTIME_BGFX_3D
        OR ASTRO_BUILD_EDITOR OR BUILD_TESTING)
    add_subdirectory(engine/render3d)
endif()

if(ASTRO_BUILD_VISUAL_LOGIC OR ASTRO_BUILD_EDITOR OR BUILD_TESTING)
    add_subdirectory(engine/logic)
endif()

if(ASTRO_BUILD_EDITOR OR BUILD_TESTING)
    add_subdirectory(engine/ui)
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

option(ASTRO_BUILD_WINDOWS_RUNTIME_PACKAGE "Package the optional Game UI runtime demo as a standalone Windows ZIP" OFF)
if(ASTRO_BUILD_STACK_SPIKE OR ASTRO_BUILD_EDITOR OR ASTRO_BUILD_GAME_UI_DEMO OR ASTRO_BUILD_2D_PLAYGROUND)
    add_subdirectory(samples/stack_spike)
endif()

if(ASTRO_BUILD_RUNTIME_BGFX_3D)
    if(NOT ASTRO_BUILD_STACK_SPIKE)
        message(FATAL_ERROR "ASTRO_BUILD_RUNTIME_BGFX_3D requires ASTRO_BUILD_STACK_SPIKE and the local BGFX dependency cache")
    endif()
    add_subdirectory(engine/runtime/adapters/bgfx)
endif()

if(ASTRO_BUILD_RENDER3D_BGFX_SMOKE OR ASTRO_BUILD_RUNTIME_BGFX_3D)
    if(NOT ASTRO_BUILD_STACK_SPIKE AND NOT ASTRO_BUILD_EDITOR)
        message(FATAL_ERROR "ASTRO_BUILD_RENDER3D_BGFX_SMOKE requires the prepared SDL3/bgfx stack dependency targets")
    endif()
    add_subdirectory(samples/3d_prototype)
endif()

if(ASTRO_BUILD_2D_PLAYGROUND OR (ASTRO_BUILD_EDITOR AND BUILD_TESTING))
    add_subdirectory(samples/2d_playground)
endif()

if(ASTRO_BUILD_GAME_UI_DEMO)
    add_subdirectory(samples/game_ui_demo)
endif()

if(ASTRO_BUILD_EDITOR)
    add_subdirectory(editor)
endif()

if(ASTRO_BUILD_WINDOWS_RUNTIME_PACKAGE)
    if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "ASTRO_BUILD_WINDOWS_RUNTIME_PACKAGE currently requires Windows x64 MSVC")
    endif()
    if(NOT ASTRO_BUILD_GAME_UI_DEMO)
        message(FATAL_ERROR "ASTRO_BUILD_WINDOWS_RUNTIME_PACKAGE requires ASTRO_BUILD_GAME_UI_DEMO=ON")
    endif()

    set(CPACK_GENERATOR "ZIP")
    set(CPACK_PACKAGE_NAME "AstroEngineGameUi")
    set(CPACK_PACKAGE_FILE_NAME "AstroEngine-Stage05.6-Windows-x64")
    set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
    include(CPack)
endif()

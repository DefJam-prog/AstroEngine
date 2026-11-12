cmake_minimum_required(VERSION 3.20)

project(AstroEngine LANGUAGES CXX)

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

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

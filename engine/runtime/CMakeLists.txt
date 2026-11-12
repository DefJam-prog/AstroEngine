add_executable(astro_headless main.cpp)
target_compile_features(astro_headless PRIVATE cxx_std_20)
set_target_properties(astro_headless PROPERTIES CXX_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(astro_headless PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8)
else()
    target_compile_options(astro_headless PRIVATE -Wall -Wextra -Wpedantic)
endif()

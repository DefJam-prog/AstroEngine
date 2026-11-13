add_test(
    NAME headless.startup
    COMMAND "${CMAKE_COMMAND}"
        "-DAPP=$<TARGET_FILE:astro_headless>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/smoke/headless_startup.cmake"
)
set_tests_properties(headless.startup PROPERTIES LABELS "smoke" TIMEOUT 15)

foreach(case IN ITEMS help invalid)
    add_test(
        NAME headless.${case}
        COMMAND "${CMAKE_COMMAND}"
            "-DAPP=$<TARGET_FILE:astro_headless>"
            "-DCASE=${case}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/smoke/headless_arguments.cmake"
    )
    set_tests_properties(headless.${case} PROPERTIES LABELS "smoke" TIMEOUT 15)
endforeach()

add_executable(astro_core_tests unit/core/CoreTests.cpp)
target_link_libraries(astro_core_tests PRIVATE astro_core)
target_compile_features(astro_core_tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(astro_core_tests PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8)
else()
    target_compile_options(astro_core_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME core.unit COMMAND astro_core_tests)
set_tests_properties(core.unit PROPERTIES LABELS "unit;core" TIMEOUT 15)

add_executable(astro_scene_tests unit/scene/SceneTests.cpp)
target_link_libraries(astro_scene_tests PRIVATE astro_scene)
target_compile_features(astro_scene_tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(astro_scene_tests PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8)
else()
    target_compile_options(astro_scene_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME scene.unit COMMAND astro_scene_tests)
set_tests_properties(scene.unit PROPERTIES LABELS "unit;scene" TIMEOUT 15)

add_executable(astro_scene_command_tests integration/scene/SceneCommandTests.cpp)
target_link_libraries(astro_scene_command_tests PRIVATE astro_scene)
target_compile_features(astro_scene_command_tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(astro_scene_command_tests PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8)
else()
    target_compile_options(astro_scene_command_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME scene.command.integration COMMAND astro_scene_command_tests)
set_tests_properties(scene.command.integration PROPERTIES LABELS "integration;scene;commands" TIMEOUT 15)

add_executable(astro_scene_file_tests integration/scene/SceneFileTests.cpp)
target_link_libraries(astro_scene_file_tests PRIVATE astro_scene)
target_compile_features(astro_scene_file_tests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(astro_scene_file_tests PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8)
else()
    target_compile_options(astro_scene_file_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME scene.file.integration COMMAND astro_scene_file_tests)
set_tests_properties(scene.file.integration PROPERTIES LABELS "integration;scene;serialization" TIMEOUT 15)

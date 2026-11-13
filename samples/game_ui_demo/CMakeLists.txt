add_library(astro_runtime_imgui_renderer3 STATIC
    "${ASTRO_SPIKE_DEPS}/imgui/backends/imgui_impl_sdlrenderer3.cpp")
target_include_directories(astro_runtime_imgui_renderer3 PUBLIC
    "${ASTRO_SPIKE_DEPS}/imgui" "${ASTRO_SPIKE_DEPS}/imgui/backends")
target_link_libraries(astro_runtime_imgui_renderer3 PUBLIC spike_imgui SDL3::SDL3-static)
target_compile_features(astro_runtime_imgui_renderer3 PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(astro_runtime_imgui_renderer3 PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8)
endif()

add_library(astro_runtime_sdl_game_ui
    "${PROJECT_SOURCE_DIR}/engine/runtime/adapters/sdl3/ImGuiGameUiAdapter.cpp")
target_include_directories(astro_runtime_sdl_game_ui PUBLIC "${PROJECT_SOURCE_DIR}/engine/runtime/include")
target_link_libraries(astro_runtime_sdl_game_ui PUBLIC astro_runtime_game_ui
    astro_runtime_imgui_renderer3 spike_imgui SDL3::SDL3-static)
target_compile_features(astro_runtime_sdl_game_ui PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(astro_runtime_sdl_game_ui PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8)
else()
    target_compile_options(astro_runtime_sdl_game_ui PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(astro_game_ui_demo main.cpp)
target_link_libraries(astro_game_ui_demo PRIVATE astro_runtime_sdl_game_ui)
target_compile_features(astro_game_ui_demo PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(astro_game_ui_demo PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8)
else()
    target_compile_options(astro_game_ui_demo PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(BUILD_TESTING)
    add_test(NAME runtime.game_ui.visual.smoke COMMAND astro_game_ui_demo --smoke
        "${PROJECT_SOURCE_DIR}/tests/visual/game_ui_main_menu.bmp")
    set_tests_properties(runtime.game_ui.visual.smoke PROPERTIES
        LABELS "smoke;runtime;ui;graphics" TIMEOUT 30 RUN_SERIAL TRUE)
endif()

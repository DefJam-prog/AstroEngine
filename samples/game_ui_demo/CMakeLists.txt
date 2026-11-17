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

add_library(astro_runtime_sdl_audio "${PROJECT_SOURCE_DIR}/engine/runtime/adapters/sdl3/SdlAudioCuePlayer.cpp")
target_include_directories(astro_runtime_sdl_audio PUBLIC "${PROJECT_SOURCE_DIR}/engine/runtime/include")
target_link_libraries(astro_runtime_sdl_audio PUBLIC SDL3::SDL3-static astro_core)
target_compile_features(astro_runtime_sdl_audio PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(astro_runtime_sdl_audio PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8)
else()
    target_compile_options(astro_runtime_sdl_audio PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(astro_game_ui_demo main.cpp)
target_link_libraries(astro_game_ui_demo PRIVATE astro_runtime_sdl_game_ui astro_runtime_sdl_audio
    astro_runtime_puzzle astro_runtime_puzzle_save)
target_compile_features(astro_game_ui_demo PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(astro_game_ui_demo PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8)
else()
    target_compile_options(astro_game_ui_demo PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(ASTRO_BUILD_WINDOWS_RUNTIME_PACKAGE)
    foreach(license IN ITEMS SDL/LICENSE.txt imgui/LICENSE.txt)
        if(NOT EXISTS "${ASTRO_SPIKE_DEPS}/${license}")
            message(FATAL_ERROR "Cannot package the runtime without third-party license file: ${ASTRO_SPIKE_DEPS}/${license}")
        endif()
    endforeach()

    install(TARGETS astro_game_ui_demo RUNTIME DESTINATION ".")
    install(FILES README-runtime.txt DESTINATION ".")
    install(FILES "${ASTRO_SPIKE_DEPS}/SDL/LICENSE.txt"
        DESTINATION licenses RENAME SDL3-LICENSE.txt)
    install(FILES "${ASTRO_SPIKE_DEPS}/imgui/LICENSE.txt"
        DESTINATION licenses RENAME Dear-ImGui-LICENSE.txt)
    install(DIRECTORY "${PROJECT_SOURCE_DIR}/samples/ПазлКампания/Puzzle"
        DESTINATION game-data/puzzle-campaign)
    install(FILES launch_puzzle_campaign.cmd DESTINATION ".")
endif()

if(BUILD_TESTING)
    add_test(NAME runtime.game_ui.puzzle.smoke COMMAND astro_game_ui_demo --smoke-puzzle)
    set_tests_properties(runtime.game_ui.puzzle.smoke PROPERTIES
        LABELS "smoke;runtime;gameplay;puzzle;ui" TIMEOUT 30 RUN_SERIAL TRUE)

    add_test(NAME runtime.game_ui.campaign.smoke COMMAND astro_game_ui_demo --project-root
        "${PROJECT_SOURCE_DIR}/samples/ПазлКампания" --campaign starter --smoke)
    set_tests_properties(runtime.game_ui.campaign.smoke PROPERTIES
        LABELS "smoke;runtime;gameplay;puzzle;campaign;ui" TIMEOUT 30 RUN_SERIAL TRUE)

    add_test(NAME runtime.game_ui.visual.smoke COMMAND astro_game_ui_demo --smoke
        "${PROJECT_SOURCE_DIR}/tests/visual/game_ui_main_menu.bmp")
    set_tests_properties(runtime.game_ui.visual.smoke PROPERTIES
        LABELS "smoke;runtime;ui;graphics" TIMEOUT 30 RUN_SERIAL TRUE)

    add_executable(astro_sdl_audio_smoke "${PROJECT_SOURCE_DIR}/tests/smoke/SdlAudioSmoke.cpp")
    target_link_libraries(astro_sdl_audio_smoke PRIVATE astro_runtime_sdl_audio)
    target_compile_features(astro_sdl_audio_smoke PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(astro_sdl_audio_smoke PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8)
    else()
        target_compile_options(astro_sdl_audio_smoke PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME runtime.audio.sdl.smoke COMMAND astro_sdl_audio_smoke)
    set_tests_properties(runtime.audio.sdl.smoke PROPERTIES LABELS "smoke;runtime;audio;SDL3"
        TIMEOUT 15 RUN_SERIAL TRUE ENVIRONMENT "SDL_AUDIODRIVER=dummy")

    if(ASTRO_BUILD_WINDOWS_RUNTIME_PACKAGE)
        find_program(ASTRO_CPACK_EXECUTABLE NAMES cpack REQUIRED)
        add_test(NAME runtime.windows.package.clean_start
            COMMAND "${CMAKE_COMMAND}"
                "-DCPACK_EXECUTABLE=${ASTRO_CPACK_EXECUTABLE}"
                "-DCPACK_CONFIG=${PROJECT_BINARY_DIR}/CPackConfig.cmake"
                "-DPACKAGE_FILE_NAME=AstroEngine-Stage05.6-Windows-x64.zip"
                "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/package-smoke"
                -P "${PROJECT_SOURCE_DIR}/tests/smoke/windows_runtime_package.cmake")
        set_tests_properties(runtime.windows.package.clean_start PROPERTIES
            LABELS "smoke;runtime;package;windows" TIMEOUT 90 RUN_SERIAL TRUE)
    endif()
endif()

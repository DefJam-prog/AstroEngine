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

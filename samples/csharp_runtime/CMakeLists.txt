find_program(ASTRO_DOTNET_EXECUTABLE NAMES dotnet.exe dotnet HINTS "${ASTRO_DOTNET_ROOT}" NO_DEFAULT_PATH REQUIRED)
set(_managed_dir "${CMAKE_CURRENT_BINARY_DIR}/managed")
set(_obj_dir "${CMAKE_CURRENT_BINARY_DIR}/obj")
set(_managed_assembly "${_managed_dir}/AstroEngine.CSharpSample.dll")
set(_managed_runtimeconfig "${_managed_dir}/AstroEngine.CSharpSample.runtimeconfig.json")

add_custom_command(
    OUTPUT "${_managed_assembly}" "${_managed_runtimeconfig}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_managed_dir}" "${_obj_dir}"
    COMMAND "${ASTRO_DOTNET_EXECUTABLE}" restore "${CMAKE_CURRENT_SOURCE_DIR}/AstroEngine.CSharpSample.csproj"
        --configfile "${CMAKE_CURRENT_SOURCE_DIR}/NuGet.Config"
        "-p:BaseIntermediateOutputPath=${_obj_dir}/"
    COMMAND "${ASTRO_DOTNET_EXECUTABLE}" build "${CMAKE_CURRENT_SOURCE_DIR}/AstroEngine.CSharpSample.csproj"
        --no-restore --configuration Release --output "${_managed_dir}"
        "-p:BaseIntermediateOutputPath=${_obj_dir}/"
    DEPENDS AstroEngine.CSharpSample.csproj NuGet.Config SceneMotion.cs
    VERBATIM
    COMMENT "Building the dependency-free C# runtime sample")
add_custom_target(astro_csharp_script_sample DEPENDS "${_managed_assembly}" "${_managed_runtimeconfig}")

if(BUILD_TESTING)
    add_executable(astro_csharp_runtime_integration_tests ../../tests/integration/runtime/CSharpRuntimeHostTests.cpp)
    target_link_libraries(astro_csharp_runtime_integration_tests PRIVATE astro_runtime_csharp astro_runtime_session astro_scene)
    target_compile_features(astro_csharp_runtime_integration_tests PRIVATE cxx_std_20)
    target_compile_options(astro_csharp_runtime_integration_tests PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8)
    add_dependencies(astro_csharp_runtime_integration_tests astro_csharp_script_sample)
    add_test(NAME runtime.csharp_host.integration
        COMMAND astro_csharp_runtime_integration_tests
            "${ASTRO_DOTNET_ROOT}" "${_managed_runtimeconfig}" "${_managed_assembly}"
            "${CMAKE_CURRENT_SOURCE_DIR}/SceneMotion.cs")
    set_tests_properties(runtime.csharp_host.integration PROPERTIES LABELS "integration;runtime;csharp" TIMEOUT 60)

    find_program(ASTRO_PWSH_EXECUTABLE NAMES pwsh pwsh.exe)
    if(ASTRO_PWSH_EXECUTABLE)
        add_test(NAME runtime.csharp_project_build.integration
            COMMAND "${ASTRO_PWSH_EXECUTABLE}" -NoProfile -File
                "${PROJECT_SOURCE_DIR}/tests/integration/runtime/CSharpProjectBuildTests.ps1"
                -RepositoryRoot "${PROJECT_SOURCE_DIR}"
                -DotnetRoot "${ASTRO_DOTNET_ROOT}"
                -WorkParent "${CMAKE_CURRENT_BINARY_DIR}/project-build-tests")
        set_tests_properties(runtime.csharp_project_build.integration PROPERTIES LABELS "integration;runtime;csharp;build" TIMEOUT 300)

        add_test(NAME runtime.csharp_metadata_validator.integration
            COMMAND "${ASTRO_PWSH_EXECUTABLE}" -NoProfile -File
                "${PROJECT_SOURCE_DIR}/tests/integration/runtime/CSharpMetadataValidatorTests.ps1"
                -RepositoryRoot "${PROJECT_SOURCE_DIR}"
                -DotnetRoot "${ASTRO_DOTNET_ROOT}"
                -ValidatorProject "${PROJECT_SOURCE_DIR}/tools/csharp/metadata-validator/AstroEngine.CSharpMetadataValidator.csproj"
                -WorkParent "${CMAKE_CURRENT_BINARY_DIR}/metadata-validator-tests")
        set_tests_properties(runtime.csharp_metadata_validator.integration PROPERTIES LABELS "integration;runtime;csharp;security" TIMEOUT 300)
    endif()
endif()

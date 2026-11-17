find_program(ASTRO_METADATA_DOTNET_EXECUTABLE NAMES dotnet.exe dotnet
    HINTS "${ASTRO_DOTNET_ROOT}" NO_DEFAULT_PATH REQUIRED)

set(_publish_dir "${CMAKE_CURRENT_BINARY_DIR}/publish")
set(_obj_dir "${CMAKE_CURRENT_BINARY_DIR}/obj")
set(_validator_dll "${_publish_dir}/AstroEngine.CSharpMetadataValidator.dll")
set(_validator_apphost "${_publish_dir}/AstroEngine.CSharpMetadataValidator.exe")
set(_validator_deps "${_publish_dir}/AstroEngine.CSharpMetadataValidator.deps.json")
set(_validator_runtimeconfig "${_publish_dir}/AstroEngine.CSharpMetadataValidator.runtimeconfig.json")

add_custom_command(
    OUTPUT "${_validator_dll}" "${_validator_apphost}" "${_validator_deps}" "${_validator_runtimeconfig}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_publish_dir}" "${_obj_dir}"
    COMMAND "${ASTRO_METADATA_DOTNET_EXECUTABLE}" restore
        "${CMAKE_CURRENT_SOURCE_DIR}/AstroEngine.CSharpMetadataValidator.csproj"
        --configfile "${CMAKE_CURRENT_SOURCE_DIR}/NuGet.Config"
        "-p:BaseIntermediateOutputPath=${_obj_dir}/"
    COMMAND "${ASTRO_METADATA_DOTNET_EXECUTABLE}" build
        "${CMAKE_CURRENT_SOURCE_DIR}/AstroEngine.CSharpMetadataValidator.csproj"
        --no-restore --configuration Release --output "${_publish_dir}"
        "-p:BaseIntermediateOutputPath=${_obj_dir}/"
    DEPENDS AstroEngine.CSharpMetadataValidator.csproj NuGet.Config Program.cs
    VERBATIM
    COMMENT "Building the offline C# metadata validator")
add_custom_target(astro_csharp_metadata_validator DEPENDS
    "${_validator_dll}" "${_validator_apphost}" "${_validator_deps}" "${_validator_runtimeconfig}")

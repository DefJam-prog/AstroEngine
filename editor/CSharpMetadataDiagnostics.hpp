#pragma once

#include "astro/assets/AssetRegistry.hpp"
#include "astro/core/Result.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace astro::editor
{
struct CSharpMetadataValidationPlan final
{
    std::filesystem::path projectRoot;
    std::filesystem::path assemblyPath;
    std::vector<std::string> managedTypes;
};

struct CSharpMetadataValidationResult final
{
    bool passed = false;
    bool timedOut = false;
    std::uint32_t exitCode = 1;
    std::size_t managedTypeCount = 0;
    std::size_t workerBatchCount = 0;
    std::string output;
};

[[nodiscard]] astro::core::Result<CSharpMetadataValidationPlan> prepareCSharpMetadataValidation(
    const astro::assets::AssetRegistry& registry, const astro::assets::ScriptBuildReport& buildReport);

[[nodiscard]] astro::core::Result<std::vector<CSharpMetadataValidationPlan>> splitCSharpMetadataValidationPlan(
    const CSharpMetadataValidationPlan& plan);

[[nodiscard]] CSharpMetadataValidationResult runCSharpMetadataValidation(
    const std::filesystem::path& dotnetRoot, const std::filesystem::path& validatorAssembly,
    const CSharpMetadataValidationPlan& plan);
}

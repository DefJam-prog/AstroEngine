#pragma once

#include "astro/core/Result.hpp"

#include <filesystem>
#include <string_view>

namespace astro::editor
{
astro::core::Result<std::filesystem::path> resolveNewProjectDirectory(
    const std::filesystem::path& parentDirectory, std::string_view projectName);
}

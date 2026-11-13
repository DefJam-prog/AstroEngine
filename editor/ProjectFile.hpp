#pragma once

#include "astro/core/Result.hpp"

#include <filesystem>
#include <string>

namespace astro::editor
{
struct ProjectInfo
{
    std::string name;
};

astro::core::Result<ProjectInfo> readProject(const std::filesystem::path& file);
astro::core::Result<ProjectInfo> createProject(const std::filesystem::path& directory, const std::string& name);
}

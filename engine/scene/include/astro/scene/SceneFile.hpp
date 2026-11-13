#pragma once

#include "astro/scene/Scene.hpp"

#include <filesystem>

namespace astro::scene
{
core::Result<void> saveSceneFile(const std::filesystem::path& file, const Scene& scene);
core::Result<Scene> loadSceneFile(const std::filesystem::path& file);
}

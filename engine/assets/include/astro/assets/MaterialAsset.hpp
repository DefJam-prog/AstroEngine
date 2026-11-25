#pragma once

#include "astro/assets/AssetRegistry.hpp"

namespace astro::assets
{
// Bounded UTF-8 line format. Version 1 is read with historical Scene View
// sampler defaults; explicit writes use version 2.
[[nodiscard]] core::Result<MaterialAssetData> parseMaterialAsset(std::string_view text);
[[nodiscard]] core::Result<std::string> serializeMaterialAsset(const MaterialAssetData& material);
[[nodiscard]] core::Result<void> validateMaterialAsset(const MaterialAssetData& material);
}

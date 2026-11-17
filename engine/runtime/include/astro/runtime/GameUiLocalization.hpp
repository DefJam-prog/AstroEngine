#pragma once

#include "astro/core/Result.hpp"

#include <filesystem>
#include <string>
#include <string_view>

namespace astro::runtime
{
enum class GameUiLanguage { English, Russian };

[[nodiscard]] GameUiLanguage gameUiLanguageFromCode(std::string_view code) noexcept;
[[nodiscard]] const char* gameUiLanguageCode(GameUiLanguage language) noexcept;
[[nodiscard]] const char* gameUiLanguageName(GameUiLanguage language) noexcept;
[[nodiscard]] std::string translateGameUiText(std::string_view source, GameUiLanguage language);
[[nodiscard]] std::string translateGameUiDiagnostic(std::string_view source, GameUiLanguage language);
[[nodiscard]] core::Result<GameUiLanguage> readGameUiLanguagePreference(const std::filesystem::path& path);
[[nodiscard]] core::Result<void> writeGameUiLanguagePreference(
    const std::filesystem::path& path, GameUiLanguage language);
}

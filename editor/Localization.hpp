#pragma once

#include "astro/core/Result.hpp"

#include <filesystem>
#include <string>
#include <string_view>

namespace astro::editor
{
enum class EditorLanguage
{
    English,
    Russian
};

[[nodiscard]] const char* languageName(EditorLanguage language) noexcept;
[[nodiscard]] const char* languageCode(EditorLanguage language) noexcept;
[[nodiscard]] const char* translateEditorText(const char* source, EditorLanguage language) noexcept;
[[nodiscard]] std::string translateEditorDiagnostic(std::string_view source, EditorLanguage language);
[[nodiscard]] core::Result<EditorLanguage> readEditorLanguage(const std::filesystem::path& settingsPath);
[[nodiscard]] core::Result<void> writeEditorLanguage(const std::filesystem::path& settingsPath,
    EditorLanguage language);
}

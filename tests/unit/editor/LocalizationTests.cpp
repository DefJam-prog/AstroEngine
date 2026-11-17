#include "Localization.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

namespace
{
int failures = 0;
void check(const bool condition, const char* message)
{
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}

bool hasLetters(const std::string& value)
{
    const std::regex formatSpecifier(R"REGEX(%[-+ #0-9.*hljztL]*[diuoxXfFeEgGaAcspn])REGEX");
    const auto words = std::regex_replace(value, formatSpecifier, "");
    return std::any_of(words.begin(), words.end(), [](const unsigned char ch) {
        return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
    });
}
}

int main()
{
    using astro::editor::EditorLanguage;
    using astro::editor::translateEditorText;
    check(std::string_view(translateEditorText("Hierarchy", EditorLanguage::English)) == "Hierarchy",
        "English uses source text");
    check(std::string_view(translateEditorText("Hierarchy", EditorLanguage::Russian)) == "Иерархия###Hierarchy",
        "Russian translation preserves stable ImGui window identity");
    check(std::string_view(translateEditorText("Language", EditorLanguage::Russian)) == "Язык",
        "settings language label is translated");
    check(std::string_view(translateEditorText("Campaign save slot", EditorLanguage::Russian))
            == "Слот сохранения кампании",
        "campaign slot selector is translated");
    check(std::string_view(translateEditorText("Saved campaign progress to slot ", EditorLanguage::Russian))
            == "Прогресс кампании сохранён в слот ",
        "campaign save confirmation is translated");
    check(std::string_view(translateEditorText("Loaded campaign progress from slot ", EditorLanguage::Russian))
            == "Прогресс кампании загружен из слота ",
        "campaign load confirmation is translated");
    check(astro::editor::translateEditorDiagnostic("No errors recorded", EditorLanguage::Russian) == "Ошибок нет",
        "known diagnostic is translated as a whole");
    check(astro::editor::translateEditorDiagnostic(
            "Registered managed type differs from the current build binding; register the current build",
            EditorLanguage::Russian)
            == "Зарегистрированный managed type не совпадает с привязкой текущей сборки; зарегистрируйте сборку заново",
        "managed type mismatch diagnostic is translated");
    check(astro::editor::translateEditorDiagnostic("asset source unavailable", EditorLanguage::Russian)
        == "Ошибка редактора: asset source unavailable",
        "unknown diagnostic is clearly marked in Russian while preserving source detail");
    check(astro::editor::translateEditorDiagnostic("Ошибка уже локализована", EditorLanguage::Russian)
        == "Ошибка уже локализована", "already localized diagnostics are not prefixed twice");
    check(astro::editor::translateEditorDiagnostic("file not found: C:\\проект\\scene.astro", EditorLanguage::Russian)
        == "Ошибка редактора: file not found: C:\\проект\\scene.astro",
        "Unicode paths do not suppress the localized diagnostic prefix");

    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto temporary = std::filesystem::temp_directory_path() / ("astro-localization-test-" + std::to_string(unique));
    const auto settingsPath = temporary / "editor-settings.cfg";
    const auto missing = astro::editor::readEditorLanguage(settingsPath);
    check(missing && missing.value() == EditorLanguage::English, "missing preference defaults to English");
    const auto savedRussian = astro::editor::writeEditorLanguage(settingsPath, EditorLanguage::Russian);
    const auto loadedRussian = astro::editor::readEditorLanguage(settingsPath);
    check(savedRussian && loadedRussian && loadedRussian.value() == EditorLanguage::Russian,
        "Russian language preference round-trips");
    const auto savedEnglish = astro::editor::writeEditorLanguage(settingsPath, EditorLanguage::English);
    const auto loadedEnglish = astro::editor::readEditorLanguage(settingsPath);
    check(savedEnglish && loadedEnglish && loadedEnglish.value() == EditorLanguage::English,
        "preference can be atomically changed back to English");
    {
        std::ofstream output(settingsPath, std::ios::binary | std::ios::trunc);
        output << "ASTRO_EDITOR_SETTINGS 99\nLANGUAGE xx\n";
    }
    check(!astro::editor::readEditorLanguage(settingsPath), "reject unsupported settings versions/languages");
    check(!astro::editor::writeEditorLanguage(settingsPath, static_cast<EditorLanguage>(99)),
        "reject unsupported language enum without publishing it");

#ifdef ASTRO_EDITOR_MAIN_SOURCE_PATH
    std::ifstream sourceFile(ASTRO_EDITOR_MAIN_SOURCE_PATH, std::ios::binary);
    const std::string source{std::istreambuf_iterator<char>(sourceFile), std::istreambuf_iterator<char>()};
    check(sourceFile.good() || sourceFile.eof(), "read editor UI source for translation coverage");
    const std::regex translatedCall(R"REGEX(ImGui::[A-Za-z_][A-Za-z_0-9]*\(\s*tr\("((?:\\.|[^"\\])*)"\))REGEX");
    std::size_t covered = 0;
    for (std::sregex_iterator it(source.begin(), source.end(), translatedCall), end; it != end; ++it)
    {
        const auto key = (*it)[1].str();
        if (!hasLetters(key)) continue; // Placeholder-only formats such as "%s" contain no UI wording.
        const auto* translated = translateEditorText(key.c_str(), EditorLanguage::Russian);
        if (std::string_view(translated) == key)
        {
            std::cerr << "UNTRANSLATED: " << key << '\n';
            check(false, "every English ImGui label has a Russian translation");
        }
        ++covered;
    }
    check(covered >= 200, "Russian catalog covers the complete editor label set");

    const std::regex directTextLiteral(R"REGEX(ImGui::[A-Za-z_][A-Za-z_0-9]*\(\s*"([^"]*)")REGEX");
    const std::vector<std::string> stableIds{
        "CheckpointMap", "EntityTree", "LogicNodes", "PuzzleBoardInput", "PuzzleCampaignCatalog",
        "PuzzleLevelBoard", "PuzzleLevelCatalog", "SceneCanvas", "SceneViewTabs", "TimelineCurve",
        "UiElementList", "UiInspector", "UiPreview", "UiPreviewCanvas"};
    for (std::sregex_iterator it(source.begin(), source.end(), directTextLiteral), end; it != end; ++it)
    {
        const auto literal = (*it)[1].str();
        const bool hidden = literal.starts_with("##");
        const bool stable = std::find(stableIds.begin(), stableIds.end(), literal) != stableIds.end();
        check(hidden || stable, "remaining raw ImGui strings are hidden IDs, not user-facing text");
    }
#endif

    std::error_code ec;
    std::filesystem::remove_all(temporary, ec);
    check(!ec, "remove isolated localization test directory");
    return failures == 0 ? 0 : 1;
}

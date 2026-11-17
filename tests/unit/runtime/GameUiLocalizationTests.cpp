#include "astro/runtime/GameUiLocalization.hpp"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

namespace
{
int failures = 0;
void check(const bool condition, const char* message)
{
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
}

int main()
{
    using namespace astro::runtime;
    check(gameUiLanguageFromCode("ru") == GameUiLanguage::Russian
        && gameUiLanguageFromCode("en") == GameUiLanguage::English,
        "language setting uses stable en/ru codes");
    check(std::string_view(translateGameUiText("Start game", GameUiLanguage::English)) == "Start game",
        "English labels remain unchanged");
    check(std::string_view(translateGameUiText("Start game", GameUiLanguage::Russian)) == "Начать игру",
        "runtime main menu is translated");
    check(std::string_view(translateGameUiText("Fullscreen", GameUiLanguage::Russian)) == "Полный экран",
        "runtime settings labels are translated");
    check(std::string_view(translateGameUiText("Select two adjacent tiles", GameUiLanguage::Russian))
        == "Выберите две соседние плитки", "Puzzle gameplay instruction is translated");
    check(std::string_view(translateGameUiText("Continue campaign", GameUiLanguage::Russian))
        == "Продолжить кампанию", "runtime campaign continuation action is translated");
    check(std::string_view(translateGameUiText("Campaign complete", GameUiLanguage::Russian))
        == "Кампания завершена", "runtime campaign completion state is translated");
    check(std::string_view(translateGameUiText("Save progress", GameUiLanguage::Russian))
        == "Сохранить прогресс" && translateGameUiText("Load progress", GameUiLanguage::Russian)
            == "Загрузить прогресс" && translateGameUiText("Save slot", GameUiLanguage::Russian)
            == "Слот сохранения", "manual campaign save controls are translated");
    check(translateGameUiDiagnostic("Campaign save slots are unavailable", GameUiLanguage::Russian)
        == "Слоты сохранения кампании недоступны", "campaign save availability diagnostic is translated");
    const std::string_view sourceSlice{"--user-label--"};
    check(translateGameUiText(sourceSlice.substr(2, 10), GameUiLanguage::English) == "user-label",
        "translation safely copies non-null-terminated source slices");
    check(std::string_view(translateGameUiText("HUD field was not found", GameUiLanguage::Russian))
        == "Поле HUD не найдено", "known runtime API diagnostic is translated");
    check(translateGameUiDiagnostic("Game UI adapter is not initialized", GameUiLanguage::Russian)
        == "Адаптер игрового интерфейса не инициализирован",
        "known adapter diagnostic is translated");
    check(translateGameUiDiagnostic("backend detail", GameUiLanguage::Russian)
        == "Ошибка игры: backend detail", "unknown diagnostic retains source detail with a localized prefix");
    check(translateGameUiDiagnostic("Could not publish Game UI language preference: Access denied",
        GameUiLanguage::Russian) == "Не удалось сохранить язык Game UI: Access denied",
        "known diagnostic templates translate while preserving backend detail");
    check(translateGameUiDiagnostic("backend detail", GameUiLanguage::English) == "backend detail",
        "English diagnostics preserve source text");
    constexpr std::array knownModelErrors{
        "Game setting ID or label is invalid", "Game setting limit reached", "Game setting ID already exists",
        "Numeric setting range or default is invalid", "Choice setting default or options are invalid",
        "Choice setting contains invalid text", "Choice setting contains duplicate options",
        "Boolean setting cannot have choice options", "Game setting was not found",
        "Settings can only be changed on the Settings screen",
        "Game setting value has the wrong type or is out of range",
        "HUD field ID, label, or text is invalid", "HUD field limit reached", "HUD field ID already exists",
        "HUD field was not found", "HUD text is invalid or too long", "Game UI event sequence exhausted",
    };
    for (const auto* source : knownModelErrors)
        check(translateGameUiDiagnostic(source, GameUiLanguage::Russian) != "Ошибка игры: " + std::string(source),
            "all GameUiModel validation diagnostics have exact Russian text");

    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto temporaryRoot = std::filesystem::temp_directory_path()
        / ("astro-game-ui-language-" + std::to_string(unique));
    const auto preferencePath = temporaryRoot / "Настройки" / "game-ui-settings.cfg";
    const auto missing = readGameUiLanguagePreference(preferencePath);
    check(missing && missing.value() == GameUiLanguage::English,
        "missing runtime language preference defaults to English");
    const auto savedRussian = writeGameUiLanguagePreference(preferencePath, GameUiLanguage::Russian);
    const auto loadedRussian = readGameUiLanguagePreference(preferencePath);
    check(savedRussian && loadedRussian && loadedRussian.value() == GameUiLanguage::Russian,
        "Russian runtime preference round-trips through a Unicode path");
    const auto savedEnglish = writeGameUiLanguagePreference(preferencePath, GameUiLanguage::English);
    const auto loadedEnglish = readGameUiLanguagePreference(preferencePath);
    check(savedEnglish && loadedEnglish && loadedEnglish.value() == GameUiLanguage::English,
        "runtime language preference can be changed back to English");
    {
        std::ofstream malformed(preferencePath, std::ios::binary | std::ios::trunc);
        malformed << "ASTRO_GAME_UI_SETTINGS 99\nLANGUAGE xx\n";
    }
    check(!readGameUiLanguagePreference(preferencePath),
        "unsupported runtime preference version or language is rejected");
    check(!writeGameUiLanguagePreference(preferencePath, static_cast<GameUiLanguage>(99)),
        "unsupported runtime language enum is rejected");
    std::error_code cleanupError;
    std::filesystem::remove_all(temporaryRoot, cleanupError);
    check(!cleanupError, "remove only the isolated test preference directory");
    return failures == 0 ? 0 : 1;
}

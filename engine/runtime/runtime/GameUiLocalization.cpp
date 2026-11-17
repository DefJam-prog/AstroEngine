#include "astro/runtime/GameUiLocalization.hpp"

#include <array>
#include <fstream>
#include <utility>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace astro::runtime
{
namespace
{
struct Entry final { std::string_view english; std::string_view russian; };
constexpr std::array entries{
    Entry{"ASTRO ENGINE", "АСТРО ДВИЖОК"}, Entry{"GAME", "ИГРА"},
    Entry{"PAUSED", "ПАУЗА"}, Entry{"SETTINGS", "НАСТРОЙКИ"},
    Entry{"Start game", "Начать игру"}, Entry{"Settings", "Настройки"},
    Entry{"Quit", "Выход"}, Entry{"Pause", "Пауза"}, Entry{"Resume", "Продолжить"},
    Entry{"Main menu", "Главное меню"}, Entry{"Back", "Назад"},
    Entry{"Score", "Счёт"}, Entry{"Level", "Уровень"},
    Entry{"UI scale", "Масштаб интерфейса"}, Entry{"Fullscreen", "Полный экран"},
    Entry{"Language", "Язык"}, Entry{"English", "Английский"}, Entry{"Russian", "Русский"},
    Entry{"Select two adjacent tiles", "Выберите две соседние плитки"},
    Entry{"Campaign", "Кампания"}, Entry{"Level complete", "Уровень пройден"},
    Entry{"Continue campaign", "Продолжить кампанию"}, Entry{"Campaign complete", "Кампания завершена"},
    Entry{"Save slot", "Слот сохранения"}, Entry{"Save progress", "Сохранить прогресс"},
    Entry{"Load progress", "Загрузить прогресс"},
    Entry{"Campaign save slots are unavailable", "Слоты сохранения кампании недоступны"},
    Entry{"Game setting ID or label is invalid", "Недопустимый ID или подпись настройки игры"},
    Entry{"Game setting limit reached", "Достигнут предел количества настроек игры"},
    Entry{"Game setting ID already exists", "ID настройки игры уже используется"},
    Entry{"Numeric setting range or default is invalid", "Недопустимый диапазон или начальное значение числовой настройки"},
    Entry{"Choice setting default or options are invalid", "Недопустимое начальное значение или варианты настройки"},
    Entry{"Choice setting contains invalid text", "Вариант настройки содержит недопустимый текст"},
    Entry{"Choice setting contains duplicate options", "В настройке есть повторяющиеся варианты"},
    Entry{"Boolean setting cannot have choice options", "Логическая настройка не может содержать варианты выбора"},
    Entry{"Game setting was not found", "Настройка игры не найдена"},
    Entry{"Settings can only be changed on the Settings screen", "Настройки можно менять только на экране настроек"},
    Entry{"Game setting value has the wrong type or is out of range", "Тип значения настройки не подходит или значение вне диапазона"},
    Entry{"HUD field ID, label, or text is invalid", "Недопустимый ID, подпись или текст поля HUD"},
    Entry{"HUD field limit reached", "Достигнут предел количества полей HUD"},
    Entry{"HUD field ID already exists", "ID поля HUD уже используется"},
    Entry{"HUD field was not found", "Поле HUD не найдено"},
    Entry{"HUD text is invalid or too long", "Текст HUD недопустим или слишком длинный"},
    Entry{"Could not inspect Game UI language preference: %s", "Не удалось проверить настройку языка Game UI: %s"},
    Entry{"Could not open Game UI language preference", "Не удалось открыть настройку языка Game UI"},
    Entry{"Game UI language preference exceeds the supported size", "Файл языка Game UI превышает допустимый размер"},
    Entry{"Game UI language preference has an unsupported format or language", "Формат или язык в настройке Game UI не поддерживается"},
    Entry{"Could not create Game UI settings directory: %s", "Не удалось создать папку настроек Game UI: %s"},
    Entry{"Could not create temporary Game UI settings file", "Не удалось создать временный файл настроек Game UI"},
    Entry{"Could not write Game UI language preference", "Не удалось записать язык Game UI"},
    Entry{"Could not publish Game UI language preference: %s", "Не удалось сохранить язык Game UI: %s"},
    Entry{"User preference path is unavailable", "Каталог настроек пользователя недоступен"},
    Entry{"Game UI event sequence exhausted", "Исчерпана последовательность событий игрового интерфейса"},
    Entry{"Game can only start from Main Menu with a stopped RuntimeSession", "Игру можно запустить только из главного меню при остановленной игровой сессии"},
    Entry{"Only active gameplay can be paused", "Приостановить можно только активную игру"},
    Entry{"Only paused gameplay can resume", "Продолжить можно только приостановленную игру"},
    Entry{"Settings open only from Main Menu or Pause", "Настройки открываются только из главного меню или паузы"},
    Entry{"Settings screen is not open", "Экран настроек не открыт"},
    Entry{"Return to menu is available from Gameplay or Pause", "Вернуться в меню можно из игры или паузы"},
    Entry{"Action is not available on this Game UI screen", "Это действие недоступно на текущем экране"},
    Entry{"Game UI adapter requires one valid SDL window and renderer", "Адаптер игрового интерфейса требует корректные окно SDL и renderer"},
    Entry{"Dear ImGui SDL3 platform initialization failed", "Не удалось инициализировать платформенную часть Dear ImGui для SDL3"},
    Entry{"Dear ImGui SDL renderer initialization failed", "Не удалось инициализировать renderer Dear ImGui для SDL"},
    Entry{"Game UI adapter is not initialized", "Адаптер игрового интерфейса не инициализирован"},
};

core::Error preferenceError(const core::ErrorCode code, std::string message)
{
    return {code, std::move(message)};
}
}

GameUiLanguage gameUiLanguageFromCode(const std::string_view code) noexcept
{
    return code == "ru" ? GameUiLanguage::Russian : GameUiLanguage::English;
}

const char* gameUiLanguageCode(const GameUiLanguage language) noexcept
{
    return language == GameUiLanguage::Russian ? "ru" : "en";
}

const char* gameUiLanguageName(const GameUiLanguage language) noexcept
{
    return language == GameUiLanguage::Russian ? "Русский" : "English";
}

std::string translateGameUiText(const std::string_view source, const GameUiLanguage language)
{
    if (language == GameUiLanguage::English) return std::string(source);
    for (const auto& entry : entries)
        if (entry.english == source) return std::string(entry.russian);
    return std::string(source);
}

std::string translateGameUiDiagnostic(const std::string_view source, const GameUiLanguage language)
{
    const auto translated = translateGameUiText(source, language);
    if (language == GameUiLanguage::English || translated != source) return translated;
    for (const auto& entry : entries)
    {
        const auto marker = entry.english.find("%s");
        if (marker == std::string_view::npos) continue;
        const auto prefix = entry.english.substr(0, marker);
        const auto suffix = entry.english.substr(marker + 2);
        if (!source.starts_with(prefix) || !source.ends_with(suffix)
            || source.size() < prefix.size() + suffix.size()) continue;
        auto message = std::string(entry.russian);
        const auto translatedMarker = message.find("%s");
        if (translatedMarker == std::string::npos) continue;
        message.replace(translatedMarker, 2,
            source.substr(prefix.size(), source.size() - prefix.size() - suffix.size()));
        return message;
    }
    return "Ошибка игры: " + std::string(source);
}

core::Result<GameUiLanguage> readGameUiLanguagePreference(const std::filesystem::path& path)
{
    std::error_code ec;
    const bool exists = std::filesystem::exists(path, ec);
    if (ec) return core::Result<GameUiLanguage>::failure(preferenceError(core::ErrorCode::IoError,
        "Could not inspect Game UI language preference: " + ec.message()));
    if (!exists) return core::Result<GameUiLanguage>::success(GameUiLanguage::English);
    std::ifstream input(path, std::ios::binary);
    if (!input) return core::Result<GameUiLanguage>::failure(preferenceError(core::ErrorCode::IoError,
        "Could not open Game UI language preference"));
    std::string content(128, '\0');
    input.read(content.data(), static_cast<std::streamsize>(content.size()));
    const auto count = static_cast<std::size_t>(input.gcount());
    if (input.bad() || count == content.size())
        return core::Result<GameUiLanguage>::failure(preferenceError(core::ErrorCode::ValidationError,
            "Game UI language preference exceeds the supported size"));
    content.resize(count);
    if (content == "ASTRO_GAME_UI_SETTINGS 1\nLANGUAGE en\n")
        return core::Result<GameUiLanguage>::success(GameUiLanguage::English);
    if (content == "ASTRO_GAME_UI_SETTINGS 1\nLANGUAGE ru\n")
        return core::Result<GameUiLanguage>::success(GameUiLanguage::Russian);
    return core::Result<GameUiLanguage>::failure(preferenceError(core::ErrorCode::ValidationError,
        "Game UI language preference has an unsupported format or language"));
}

core::Result<void> writeGameUiLanguagePreference(const std::filesystem::path& path,
    const GameUiLanguage language)
{
    if (language != GameUiLanguage::English && language != GameUiLanguage::Russian)
        return core::Result<void>::failure(preferenceError(core::ErrorCode::ValidationError,
            "Unsupported Game UI language"));
    std::error_code ec;
    if (!path.parent_path().empty())
    {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) return core::Result<void>::failure(preferenceError(core::ErrorCode::IoError,
            "Could not create Game UI settings directory: " + ec.message()));
    }
    auto temporary = path;
    temporary += L".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) return core::Result<void>::failure(preferenceError(core::ErrorCode::IoError,
            "Could not create temporary Game UI settings file"));
        output << "ASTRO_GAME_UI_SETTINGS 1\nLANGUAGE " << gameUiLanguageCode(language) << '\n';
        output.flush();
        if (!output)
        {
            std::filesystem::remove(temporary, ec);
            return core::Result<void>::failure(preferenceError(core::ErrorCode::IoError,
                "Could not write Game UI language preference"));
        }
    }
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        const auto error = std::error_code(static_cast<int>(GetLastError()), std::system_category()).message();
        std::filesystem::remove(temporary, ec);
        return core::Result<void>::failure(preferenceError(core::ErrorCode::IoError,
            "Could not publish Game UI language preference: " + error));
    }
#else
    std::filesystem::rename(temporary, path, ec);
    if (ec)
    {
        std::filesystem::remove(temporary, ec);
        return core::Result<void>::failure(preferenceError(core::ErrorCode::IoError,
            "Could not publish Game UI language preference: " + ec.message()));
    }
#endif
    return core::Result<void>::success();
}
}

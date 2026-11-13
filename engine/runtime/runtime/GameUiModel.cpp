#include "astro/runtime/GameUiModel.hpp"

#include <algorithm>
#include <cmath>
#include <string_view>
#include <utility>

namespace astro::runtime
{
namespace
{
constexpr std::size_t maximumSettings = 128;
constexpr std::size_t maximumHudFields = 128;

core::Error uiError(const core::ErrorCode code, std::string message)
{
    return {code, std::move(message)};
}

bool validId(const std::string_view value)
{
    if (value.empty() || value.size() > 64) return false;
    for (const auto character : value)
    {
        const bool valid = (character >= 'a' && character <= 'z')
            || (character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9')
            || character == '_' || character == '.' || character == '-';
        if (!valid) return false;
    }
    return true;
}

bool validUtf8(const std::string_view value)
{
    const auto continuation = [&value](const std::size_t index) {
        return index < value.size() && (static_cast<unsigned char>(value[index]) & 0xC0) == 0x80;
    };
    for (std::size_t i = 0; i < value.size();)
    {
        const auto lead = static_cast<unsigned char>(value[i]);
        if (lead <= 0x7F) { ++i; continue; }
        if (lead >= 0xC2 && lead <= 0xDF && continuation(i + 1)) { i += 2; continue; }
        if (lead >= 0xE0 && lead <= 0xEF && continuation(i + 1) && continuation(i + 2))
        {
            const auto second = static_cast<unsigned char>(value[i + 1]);
            if ((lead != 0xE0 || second >= 0xA0) && (lead != 0xED || second < 0xA0)) { i += 3; continue; }
        }
        if (lead >= 0xF0 && lead <= 0xF4 && continuation(i + 1) && continuation(i + 2) && continuation(i + 3))
        {
            const auto second = static_cast<unsigned char>(value[i + 1]);
            if ((lead != 0xF0 || second >= 0x90) && (lead != 0xF4 || second <= 0x8F)) { i += 4; continue; }
        }
        return false;
    }
    return true;
}

bool validText(const std::string_view value, const std::size_t maxBytes)
{
    if (value.empty() || value.size() > maxBytes || !validUtf8(value)) return false;
    for (const auto character : value)
        if (static_cast<unsigned char>(character) < 0x20 && character != '\t') return false;
    return true;
}

bool validSettingValue(const GameSetting& setting, const GameSettingValue& value)
{
    if (value.index() != setting.value.index()) return false;
    if (const auto* number = std::get_if<double>(&value))
        return std::isfinite(*number) && *number >= setting.minimum && *number <= setting.maximum;
    if (const auto* text = std::get_if<std::string>(&value))
        return std::find(setting.choices.begin(), setting.choices.end(), *text) != setting.choices.end();
    return true;
}
}

std::vector<GameUiAction> GameUiModel::availableActions() const
{
    switch (screen_)
    {
    case GameUiScreen::MainMenu: return {GameUiAction::StartGame, GameUiAction::OpenSettings, GameUiAction::Quit};
    case GameUiScreen::Gameplay: return {GameUiAction::Pause};
    case GameUiScreen::Paused: return {GameUiAction::Resume, GameUiAction::OpenSettings, GameUiAction::ReturnToMenu};
    case GameUiScreen::Settings: return {GameUiAction::Back};
    }
    return {};
}

core::Result<void> GameUiModel::addSetting(GameSettingDefinition definition)
{
    if (!validId(definition.id) || !validText(definition.label, 120))
        return core::Result<void>::failure(uiError(core::ErrorCode::ValidationError, "Game setting ID or label is invalid"));
    if (settings_.size() >= maximumSettings && !settings_.contains(definition.id))
        return core::Result<void>::failure(uiError(core::ErrorCode::ValidationError, "Game setting limit reached"));

    GameSetting setting{std::move(definition.id), std::move(definition.label),
        std::move(definition.initialValue), definition.minimum, definition.maximum, std::move(definition.choices)};
    if (const auto* number = std::get_if<double>(&setting.value))
    {
        if (!std::isfinite(*number) || !std::isfinite(setting.minimum) || !std::isfinite(setting.maximum)
            || setting.minimum >= setting.maximum || *number < setting.minimum || *number > setting.maximum
            || !setting.choices.empty())
            return core::Result<void>::failure(uiError(core::ErrorCode::ValidationError, "Numeric setting range or default is invalid"));
    }
    else if (const auto* text = std::get_if<std::string>(&setting.value))
    {
        if (setting.choices.empty() || setting.choices.size() > 128
            || !validText(*text, 120) || std::find(setting.choices.begin(), setting.choices.end(), *text) == setting.choices.end())
            return core::Result<void>::failure(uiError(core::ErrorCode::ValidationError, "Choice setting default or options are invalid"));
        for (const auto& choice : setting.choices)
            if (!validText(choice, 120))
                return core::Result<void>::failure(uiError(core::ErrorCode::ValidationError, "Choice setting contains invalid text"));
        auto uniqueChoices = setting.choices;
        std::sort(uniqueChoices.begin(), uniqueChoices.end());
        if (std::adjacent_find(uniqueChoices.begin(), uniqueChoices.end()) != uniqueChoices.end())
            return core::Result<void>::failure(uiError(core::ErrorCode::ValidationError, "Choice setting contains duplicate options"));
    }
    else if (!setting.choices.empty())
        return core::Result<void>::failure(uiError(core::ErrorCode::ValidationError, "Boolean setting cannot have choice options"));

    if (!settings_.emplace(setting.id, std::move(setting)).second)
        return core::Result<void>::failure(uiError(core::ErrorCode::AlreadyExists, "Game setting ID already exists"));
    return core::Result<void>::success();
}

core::Result<void> GameUiModel::setSetting(const std::string_view id, GameSettingValue value,
    core::EventBus& events)
{
    if (screen_ != GameUiScreen::Settings)
        return core::Result<void>::failure(uiError(core::ErrorCode::InvalidState, "Settings can only be changed on the Settings screen"));
    const auto found = settings_.find(std::string(id));
    if (found == settings_.end())
        return core::Result<void>::failure(uiError(core::ErrorCode::NotFound, "Game setting was not found"));
    if (!validSettingValue(found->second, value))
        return core::Result<void>::failure(uiError(core::ErrorCode::ValidationError, "Game setting value has the wrong type or is out of range"));
    found->second.value = std::move(value);
    return publish(events, GameUiEventKind::SettingChanged, found->first, found->second.value);
}

core::Result<void> GameUiModel::addHudField(std::string id, std::string label, std::string initialText)
{
    if (!validId(id) || !validText(label, 120) || initialText.size() > 256 || !validUtf8(initialText))
        return core::Result<void>::failure(uiError(core::ErrorCode::ValidationError, "HUD field ID, label, or text is invalid"));
    if (hudFields_.size() >= maximumHudFields && !hudFields_.contains(id))
        return core::Result<void>::failure(uiError(core::ErrorCode::ValidationError, "HUD field limit reached"));
    const auto key = id;
    if (!hudFields_.emplace(key, HudField{std::move(id), std::move(label), std::move(initialText)}).second)
        return core::Result<void>::failure(uiError(core::ErrorCode::AlreadyExists, "HUD field ID already exists"));
    return core::Result<void>::success();
}

core::Result<void> GameUiModel::setHudText(const std::string_view id, std::string text)
{
    const auto found = hudFields_.find(std::string(id));
    if (found == hudFields_.end())
        return core::Result<void>::failure(uiError(core::ErrorCode::NotFound, "HUD field was not found"));
    if (text.size() > 256 || !validUtf8(text))
        return core::Result<void>::failure(uiError(core::ErrorCode::ValidationError, "HUD text is invalid or too long"));
    found->second.text = std::move(text);
    return core::Result<void>::success();
}

core::Result<void> GameUiModel::removeHudField(const std::string_view id)
{
    if (!hudFields_.erase(std::string(id)))
        return core::Result<void>::failure(uiError(core::ErrorCode::NotFound, "HUD field was not found"));
    return core::Result<void>::success();
}

core::Result<void> GameUiModel::publish(core::EventBus& events, const GameUiEventKind kind,
    std::string settingId, GameSettingValue settingValue)
{
    if (nextEventSequence_ == 0)
        return core::Result<void>::failure(uiError(core::ErrorCode::InternalError, "Game UI event sequence exhausted"));
    const GameUiEvent event{nextEventSequence_++, kind, screen_, std::move(settingId), std::move(settingValue)};
    return events.publish(event);
}

core::Result<void> GameUiModel::startGame(RuntimeSession& session, const scene::Scene& authoringScene,
    FixedStep fixedStep, core::EventBus& events)
{
    if (screen_ != GameUiScreen::MainMenu || session.state() != RuntimeState::Stopped)
        return core::Result<void>::failure(uiError(core::ErrorCode::InvalidState, "Game can only start from Main Menu with a stopped RuntimeSession"));
    const auto started = session.play(authoringScene, std::move(fixedStep));
    if (!started) return started;
    screen_ = GameUiScreen::Gameplay;
    return publish(events, GameUiEventKind::StartRequested);
}

core::Result<void> GameUiModel::pause(RuntimeSession& session, core::EventBus& events)
{
    if (screen_ != GameUiScreen::Gameplay || session.state() != RuntimeState::Playing)
        return core::Result<void>::failure(uiError(core::ErrorCode::InvalidState, "Only active gameplay can be paused"));
    const auto pausedResult = session.pause();
    if (!pausedResult) return pausedResult;
    screen_ = GameUiScreen::Paused;
    return publish(events, GameUiEventKind::PauseChanged);
}

core::Result<void> GameUiModel::resume(RuntimeSession& session, core::EventBus& events)
{
    if (screen_ != GameUiScreen::Paused || session.state() != RuntimeState::Paused)
        return core::Result<void>::failure(uiError(core::ErrorCode::InvalidState, "Only paused gameplay can resume"));
    const auto resumed = session.resume();
    if (!resumed) return resumed;
    screen_ = GameUiScreen::Gameplay;
    return publish(events, GameUiEventKind::PauseChanged);
}

core::Result<void> GameUiModel::openSettings(core::EventBus& events)
{
    if (screen_ != GameUiScreen::MainMenu && screen_ != GameUiScreen::Paused)
        return core::Result<void>::failure(uiError(core::ErrorCode::InvalidState, "Settings open only from Main Menu or Pause"));
    settingsReturnScreen_ = screen_;
    screen_ = GameUiScreen::Settings;
    return publish(events, GameUiEventKind::SettingsOpened);
}

core::Result<void> GameUiModel::closeSettings(core::EventBus& events)
{
    if (screen_ != GameUiScreen::Settings)
        return core::Result<void>::failure(uiError(core::ErrorCode::InvalidState, "Settings screen is not open"));
    screen_ = settingsReturnScreen_;
    return publish(events, GameUiEventKind::SettingsClosed);
}

core::Result<void> GameUiModel::returnToMenu(RuntimeSession& session, core::EventBus& events)
{
    if (screen_ != GameUiScreen::Gameplay && screen_ != GameUiScreen::Paused)
        return core::Result<void>::failure(uiError(core::ErrorCode::InvalidState, "Return to menu is available from Gameplay or Pause"));
    session.stop();
    screen_ = GameUiScreen::MainMenu;
    return publish(events, GameUiEventKind::ReturnedToMenu);
}

core::Result<void> GameUiModel::requestQuit(core::EventBus& events)
{
    return publish(events, GameUiEventKind::QuitRequested);
}
}

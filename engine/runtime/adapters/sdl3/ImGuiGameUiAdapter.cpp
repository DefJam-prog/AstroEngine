#include "astro/runtime/ImGuiGameUiAdapter.hpp"

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>

namespace astro::runtime
{
ImGuiGameUiAdapter::ImGuiGameUiAdapter(GameUiModel& model, RuntimeSession& session,
    const scene::Scene& authoringScene, FixedStep fixedStep, core::EventBus& events)
    : model_(model), session_(session), authoringScene_(authoringScene),
      fixedStep_(std::move(fixedStep)), events_(events)
{
}

ImGuiGameUiAdapter::~ImGuiGameUiAdapter() { shutdown(); }

core::Result<void> ImGuiGameUiAdapter::fail(const core::ErrorCode code, std::string message)
{
    lastError_ = std::move(message);
    return core::Result<void>::failure({code, lastError_});
}

core::Result<void> ImGuiGameUiAdapter::initialize(SDL_Window* window, SDL_Renderer* renderer)
{
    if (contextReady_ || !window || !renderer)
        return fail(core::ErrorCode::InvalidArgument, "Game UI adapter requires one valid SDL window and renderer");
    window_ = window;
    renderer_ = renderer;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    contextReady_ = true;
    ImGui::StyleColorsDark();
    auto& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    if (!ImGui_ImplSDL3_InitForSDLRenderer(window_, renderer_))
    {
        shutdown();
        return fail(core::ErrorCode::InternalError, "Dear ImGui SDL3 platform initialization failed");
    }
    platformReady_ = true;
    if (!ImGui_ImplSDLRenderer3_Init(renderer_))
    {
        shutdown();
        return fail(core::ErrorCode::InternalError, "Dear ImGui SDL renderer initialization failed");
    }
    rendererReady_ = true;
    lastError_.clear();
    return core::Result<void>::success();
}

void ImGuiGameUiAdapter::shutdown() noexcept
{
    if (rendererReady_) ImGui_ImplSDLRenderer3_Shutdown();
    if (platformReady_) ImGui_ImplSDL3_Shutdown();
    if (contextReady_) ImGui::DestroyContext();
    rendererReady_ = false;
    platformReady_ = false;
    contextReady_ = false;
    renderer_ = nullptr;
    window_ = nullptr;
}

void ImGuiGameUiAdapter::processEvent(const SDL_Event& event)
{
    if (platformReady_) ImGui_ImplSDL3_ProcessEvent(&event);
    if (event.type == SDL_EVENT_QUIT) quitRequested_ = true;
}

core::Result<void> ImGuiGameUiAdapter::activate(const GameUiAction action)
{
    core::Result<void> result = core::Result<void>::failure(
        {core::ErrorCode::InvalidState, "Action is not available on this Game UI screen"});
    switch (action)
    {
    case GameUiAction::StartGame:
        result = model_.startGame(session_, authoringScene_, fixedStep_, events_);
        break;
    case GameUiAction::Pause:
        result = model_.pause(session_, events_);
        break;
    case GameUiAction::Resume:
        result = model_.resume(session_, events_);
        break;
    case GameUiAction::OpenSettings:
        result = model_.openSettings(events_);
        break;
    case GameUiAction::Back:
        result = model_.closeSettings(events_);
        break;
    case GameUiAction::ReturnToMenu:
        result = model_.returnToMenu(session_, events_);
        break;
    case GameUiAction::Quit:
        result = model_.requestQuit(events_);
        if (result) quitRequested_ = true;
        break;
    }
    if (!result) lastError_ = result.error().message;
    else lastError_.clear();
    return result;
}

core::Result<void> ImGuiGameUiAdapter::drawSettings()
{
    for (const auto& [id, setting] : model_.settings())
    {
        ImGui::PushID(id.c_str());
        if (const auto* value = std::get_if<bool>(&setting.value))
        {
            auto changedValue = *value;
            if (ImGui::Checkbox(setting.label.c_str(), &changedValue))
            {
                const auto changed = model_.setSetting(id, changedValue, events_);
                if (!changed) { ImGui::PopID(); return changed; }
            }
        }
        else if (const auto* numericValue = std::get_if<double>(&setting.value))
        {
            auto changedValue = *numericValue;
            const auto minimum = setting.minimum;
            const auto maximum = setting.maximum;
            if (ImGui::SliderScalar(setting.label.c_str(), ImGuiDataType_Double,
                    &changedValue, &minimum, &maximum, "%.2f"))
            {
                const auto changed = model_.setSetting(id, changedValue, events_);
                if (!changed) { ImGui::PopID(); return changed; }
            }
        }
        else
        {
            const auto& current = std::get<std::string>(setting.value);
            if (ImGui::BeginCombo(setting.label.c_str(), current.c_str()))
            {
                for (const auto& choice : setting.choices)
                {
                    const bool selected = choice == current;
                    if (ImGui::Selectable(choice.c_str(), selected))
                    {
                        const auto changed = model_.setSetting(id, choice, events_);
                        if (!changed) { ImGui::EndCombo(); ImGui::PopID(); return changed; }
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        }
        ImGui::PopID();
    }
    return core::Result<void>::success();
}

core::Result<void> ImGuiGameUiAdapter::render()
{
    if (!rendererReady_ || !platformReady_)
        return fail(core::ErrorCode::InvalidState, "Game UI adapter is not initialized");

    const auto scale = model_.settings().find("ui.scale");
    if (scale != model_.settings().end())
        if (const auto* value = std::get_if<double>(&scale->second.value))
            ImGui::GetIO().FontGlobalScale = static_cast<float>(*value);

    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(viewport->Size, ImGuiCond_Always);
    constexpr auto flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin("AstroEngine Game UI", nullptr, flags);
    const auto available = ImGui::GetContentRegionAvail();
    const auto cardWidth = std::min(available.x - 32.0f, 560.0f);
    ImGui::SetCursorPosX((available.x - cardWidth) * 0.5f);
    ImGui::BeginChild("Game UI card", ImVec2(cardWidth, available.y - 32.0f), true);
    ImGui::Spacing();
    ImGui::SetWindowFontScale(1.35f);
    const char* title = "ASTRO ENGINE";
    switch (model_.screen())
    {
    case GameUiScreen::MainMenu: title = "ASTRO ENGINE"; break;
    case GameUiScreen::Gameplay: title = "GAME"; break;
    case GameUiScreen::Paused: title = "PAUSED"; break;
    case GameUiScreen::Settings: title = "SETTINGS"; break;
    }
    ImGui::SetCursorPosX((cardWidth - ImGui::CalcTextSize(title).x) * 0.5f);
    ImGui::TextUnformatted(title);
    ImGui::SetWindowFontScale(1.0f);
    ImGui::Separator();
    ImGui::Spacing();

    if (model_.screen() == GameUiScreen::Gameplay || model_.screen() == GameUiScreen::Paused)
    {
        for (const auto& [id, field] : model_.hudFields())
        {
            (void)id;
            ImGui::Text("%s: %s", field.label.c_str(), field.text.c_str());
        }
        ImGui::Spacing();
    }

    core::Result<void> uiResult = core::Result<void>::success();
    const auto buttonWidth = std::max(160.0f, cardWidth - 48.0f);
    const auto drawButton = [&](const char* label, const GameUiAction action) {
        if (ImGui::Button(label, ImVec2(buttonWidth, 42.0f))) uiResult = activate(action);
    };
    switch (model_.screen())
    {
    case GameUiScreen::MainMenu:
        drawButton("Start game", GameUiAction::StartGame);
        drawButton("Settings", GameUiAction::OpenSettings);
        drawButton("Quit", GameUiAction::Quit);
        break;
    case GameUiScreen::Gameplay:
        drawButton("Pause", GameUiAction::Pause);
        break;
    case GameUiScreen::Paused:
        drawButton("Resume", GameUiAction::Resume);
        drawButton("Settings", GameUiAction::OpenSettings);
        drawButton("Main menu", GameUiAction::ReturnToMenu);
        break;
    case GameUiScreen::Settings:
        uiResult = drawSettings();
        drawButton("Back", GameUiAction::Back);
        break;
    }
    if (!uiResult) lastError_ = uiResult.error().message;
    else lastError_.clear();
    if (!lastError_.empty())
    {
        ImGui::Spacing();
        ImGui::TextWrapped("%s", lastError_.c_str());
    }
    ImGui::EndChild();
    ImGui::End();
    ImGui::Render();
    lastRenderedVertexCount_ = static_cast<std::size_t>(ImGui::GetDrawData()->TotalVtxCount);

    if (!SDL_SetRenderDrawColor(renderer_, 15, 20, 32, 255) || !SDL_RenderClear(renderer_))
        return fail(core::ErrorCode::IoError, SDL_GetError());
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer_);
    SDL_RenderPresent(renderer_);
    if (!uiResult) return uiResult;
    return core::Result<void>::success();
}
}

#pragma once

#include "astro/runtime/GameUiModel.hpp"

#include <SDL3/SDL.h>

#include <cstddef>
#include <string>

namespace astro::runtime
{
// Optional SDL3 + Dear ImGui presentation adapter. It does not depend on editor code.
class ImGuiGameUiAdapter final
{
public:
    ImGuiGameUiAdapter(GameUiModel& model, RuntimeSession& session,
        const scene::Scene& authoringScene, FixedStep fixedStep, core::EventBus& events);
    ~ImGuiGameUiAdapter();
    ImGuiGameUiAdapter(const ImGuiGameUiAdapter&) = delete;
    ImGuiGameUiAdapter& operator=(const ImGuiGameUiAdapter&) = delete;

    core::Result<void> initialize(SDL_Window* window, SDL_Renderer* renderer);
    void processEvent(const SDL_Event& event);
    core::Result<void> render();
    [[nodiscard]] bool quitRequested() const noexcept { return quitRequested_; }
    [[nodiscard]] const std::string& lastError() const noexcept { return lastError_; }
    [[nodiscard]] std::size_t lastRenderedVertexCount() const noexcept { return lastRenderedVertexCount_; }

private:
    core::Result<void> activate(GameUiAction action);
    core::Result<void> drawSettings();
    core::Result<void> fail(const core::ErrorCode code, std::string message);
    void shutdown() noexcept;

    GameUiModel& model_;
    RuntimeSession& session_;
    const scene::Scene& authoringScene_;
    FixedStep fixedStep_;
    core::EventBus& events_;
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    bool contextReady_ = false;
    bool platformReady_ = false;
    bool rendererReady_ = false;
    bool quitRequested_ = false;
    std::size_t lastRenderedVertexCount_ = 0;
    std::string lastError_;
};
}

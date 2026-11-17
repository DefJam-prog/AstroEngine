#include "astro/runtime/ImGuiGameUiAdapter.hpp"
#include "astro/runtime/GameUiLocalization.hpp"
#include "astro/runtime/PuzzleCampaignSaveAdapter.hpp"
#include "astro/runtime/PuzzleCampaignSession.hpp"
#include "astro/runtime/PuzzleGame.hpp"
#include "astro/runtime/SdlAudioCuePlayer.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstdint>
#include <cmath>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <charconv>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace
{
std::filesystem::path pathFromUtf8(const std::string_view value)
{
    std::u8string encoded;
    encoded.reserve(value.size());
    for (const unsigned char byte : value) encoded.push_back(static_cast<char8_t>(byte));
    return std::filesystem::path(encoded);
}

class SdlLifetime final
{
public:
    SdlLifetime()
    {
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) throw std::runtime_error(SDL_GetError());
    }
    ~SdlLifetime() { SDL_Quit(); }
    SdlLifetime(const SdlLifetime&) = delete;
    SdlLifetime& operator=(const SdlLifetime&) = delete;
};

class Demo final
{
public:
    Demo(const bool smoke, const bool puzzleSmoke, const bool campaignSmoke,
        std::filesystem::path campaignProjectRoot, std::string campaignId,
        const bool updateGolden, std::string goldenPath, SDL_Window* window, SDL_Renderer* renderer)
        : smoke_(smoke), puzzleSmoke_(puzzleSmoke), campaignSmoke_(campaignSmoke),
          campaignProjectRoot_(std::move(campaignProjectRoot)), campaignId_(std::move(campaignId)),
          updateGolden_(updateGolden), goldenPath_(std::move(goldenPath)),
          window_(window), renderer_(renderer), scene_(1, "Game UI demo"),
          adapter_(ui_, session_, scene_, [](astro::scene::Scene&, const double) {
              return astro::core::Result<void>::success();
          }, events_)
    {
    }

    ~Demo()
    {
        if (smoke_ && !languagePreferencePath_.empty())
        {
            std::error_code ec;
            std::filesystem::remove_all(languagePreferencePath_.parent_path(), ec);
        }
    }

    int run()
    {
        auto initialLanguage = astro::runtime::GameUiLanguage::English;
        std::string languagePreferenceDiagnostic;
        if (!smoke_)
        {
            std::unique_ptr<char, decltype(&SDL_free)> preferenceRoot(
                SDL_GetPrefPath("AstroEngine", "AstroEngine"), SDL_free);
            if (!preferenceRoot)
                languagePreferenceDiagnostic = "User preference path is unavailable";
            else
            {
                const auto preferencePath = std::filesystem::path(
                    std::u8string{reinterpret_cast<const char8_t*>(preferenceRoot.get())});
                userDataRoot_ = preferencePath;
                languagePreferencePath_ = preferencePath / "game-ui-settings.cfg";
                const auto preference = astro::runtime::readGameUiLanguagePreference(languagePreferencePath_);
                if (preference) initialLanguage = preference.value();
                else languagePreferenceDiagnostic = preference.error().message;
            }
        }
        else
        {
            const auto smokeDirectory = std::filesystem::temp_directory_path()
                / ("astro-game-ui-smoke-" + std::to_string(
                    std::chrono::steady_clock::now().time_since_epoch().count()));
            std::error_code ec;
            if (!std::filesystem::create_directory(smokeDirectory, ec) || ec)
                throw std::runtime_error("Could not create isolated Game UI smoke settings directory");
            languagePreferencePath_ = smokeDirectory / "game-ui-settings.cfg";
            userDataRoot_ = smokeDirectory;
        }
        const auto player = scene_.createEntity("Demo player");
        if (!player) throw std::runtime_error(player.error().message);
        if (!ui_.addHudField("hud.score", "Score", "0")
            || !ui_.addHudField("hud.level", "Level", "1")
            || !ui_.addSetting({"ui.scale", "UI scale", 1.0, 0.75, 1.5, {}})
            || !ui_.addSetting({"ui.language", "Language",
                std::string{astro::runtime::gameUiLanguageCode(initialLanguage)}, 0.0, 1.0, {"en", "ru"}})
            || !ui_.addSetting({"display.fullscreen", "Fullscreen", false, 0.0, 1.0, {}}))
            throw std::runtime_error("Failed to initialize Game UI model");
        const auto puzzleReady = campaignProjectRoot_.empty() ? resetPuzzle() : resetCampaign();
        if (!puzzleReady) throw std::runtime_error(puzzleReady.error().message);
        adapter_.setGameplayContentRenderer([this] { return drawPuzzleBoard(); });
        const auto subscription = events_.subscribe<astro::runtime::GameUiEvent>([this](const auto& event) {
            if (event.kind == astro::runtime::GameUiEventKind::SettingChanged
                && event.settingId == "ui.language")
            {
                if (languagePreferencePath_.empty())
                    adapter_.setPersistentDiagnostic("User preference path is unavailable");
                else
                {
                    const auto language = astro::runtime::gameUiLanguageFromCode(
                        std::get<std::string>(event.settingValue));
                    const auto saved = astro::runtime::writeGameUiLanguagePreference(languagePreferencePath_, language);
                    adapter_.setPersistentDiagnostic(saved ? std::string{} : saved.error().message);
                }
            }
            if (event.kind == astro::runtime::GameUiEventKind::SettingChanged
                && event.settingId == "display.fullscreen")
            {
                if (!SDL_SetWindowFullscreen(window_, std::get<bool>(event.settingValue)))
                    throw std::runtime_error(SDL_GetError());
            }
            if (event.kind == astro::runtime::GameUiEventKind::StartRequested)
            {
                const auto reset = resetActiveGame();
                if (!reset) adapter_.setPersistentDiagnostic(reset.error().message);
            }
            if (!smoke_ && event.kind == astro::runtime::GameUiEventKind::StartRequested)
                (void)audio_.play(astro::runtime::AudioCue::Select);
            if (!smoke_ && event.kind == astro::runtime::GameUiEventKind::PauseChanged)
                (void)audio_.play(event.screen == astro::runtime::GameUiScreen::Paused
                    ? astro::runtime::AudioCue::Select : astro::runtime::AudioCue::Match);
            if (!smoke_ && event.kind == astro::runtime::GameUiEventKind::ReturnedToMenu)
                (void)audio_.play(astro::runtime::AudioCue::LevelUp);
        });
        if (!subscription) throw std::runtime_error(subscription.error().message);
        const auto initialized = adapter_.initialize(window_, renderer_);
        if (!initialized) throw std::runtime_error(initialized.error().message);
        adapter_.setPersistentDiagnostic(std::move(languagePreferenceDiagnostic));
        if (!smoke_)
        {
            const auto audioOpened = audio_.open();
            if (!audioOpened) std::cerr << "Audio disabled: " << audioOpened.error().message << '\n';
        }
        if (puzzleSmoke_) return runPuzzleSmoke();
        if (campaignSmoke_) return runCampaignSmoke();
        if (smoke_) return runSmoke();

        bool running = true;
        auto previousTime = SDL_GetTicksNS();
        while (running && !adapter_.quitRequested())
        {
            SDL_Event event;
            while (SDL_PollEvent(&event))
            {
                adapter_.processEvent(event);
                if (event.type == SDL_EVENT_QUIT) running = false;
            }
            const auto now = SDL_GetTicksNS();
            const auto elapsed = static_cast<double>(now - previousTime) / 1'000'000'000.0;
            previousTime = now;
            if (session_.state() == astro::runtime::RuntimeState::Playing
                || session_.state() == astro::runtime::RuntimeState::Paused)
            {
                const auto frame = session_.tick(elapsed);
                if (!frame) std::cerr << "Runtime tick: " << frame.error().message << '\n';
            }
            const auto rendered = adapter_.render();
            if (!rendered) std::cerr << "Game UI render: " << rendered.error().message << '\n';
            SDL_Delay(8);
        }
        (void)events_.unsubscribe(subscription.value());
        return 0;
    }

private:
    using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;

    astro::core::Result<void> resetPuzzle()
    {
        auto created = astro::runtime::PuzzleGame::create(8, 8, 9123, 5, 300);
        if (!created) return astro::core::Result<void>::failure(created.error());
        puzzle_ = std::move(created).value();
        selectedPuzzleCell_.reset();
        pendingPuzzleMove_.reset();
        lastPuzzleMoveAccepted_ = false;
        return updatePuzzleHud();
    }

    astro::core::Result<void> resetCampaign()
    {
        campaignSession_.reset();
        puzzle_.reset();
        selectedPuzzleCell_.reset();
        pendingPuzzleMove_.reset();
        campaignLevelComplete_ = false;
        lastPuzzleMoveAccepted_ = false;
        auto started = astro::runtime::PuzzleCampaignSession::start(campaignProjectRoot_, campaignId_);
        if (!started) return astro::core::Result<void>::failure(started.error());
        campaignSession_ = std::move(started).value();
        const auto saveReady = configureCampaignSaveStore();
        if (!saveReady) return saveReady;
        return updatePuzzleHud();
    }

    std::filesystem::path campaignSaveDirectory() const
    {
        std::error_code ec;
        auto canonicalRoot = std::filesystem::weakly_canonical(campaignProjectRoot_, ec);
        if (ec) canonicalRoot = std::filesystem::absolute(campaignProjectRoot_, ec).lexically_normal();
        if (ec) throw std::runtime_error("Could not resolve the project path for campaign save identity");
        const auto utf8 = canonicalRoot.u8string();
        std::uint64_t hash = 14695981039346656037ULL;
        for (const auto byte : utf8)
        {
            hash ^= static_cast<std::uint8_t>(byte);
            hash *= 1099511628211ULL;
        }
        char hashText[17]{};
        const auto converted = std::to_chars(hashText, hashText + 16, hash, 16);
        if (converted.ec != std::errc{}) throw std::runtime_error("Could not encode project save identity");
        std::string projectKey(hashText, converted.ptr);
        return userDataRoot_ / "PuzzleCampaigns" / projectKey / campaignId_;
    }

    astro::core::Result<void> configureCampaignSaveStore()
    {
        if (userDataRoot_.empty())
            return astro::core::Result<void>::failure({astro::core::ErrorCode::IoError,
                "User preference path is unavailable"});
        auto profile = astro::runtime::SaveProfile::create("puzzle-campaign-progress", 1);
        if (!profile) return astro::core::Result<void>::failure(profile.error());
        auto selected = astro::runtime::selectPuzzleCampaignProgressFields(profile.value());
        if (!selected) return selected;
        auto store = astro::runtime::SaveStore::open(campaignSaveDirectory(), 10);
        if (!store) return astro::core::Result<void>::failure(store.error());
        campaignSaveProfile_ = std::move(profile).value();
        campaignSaveStore_ = std::move(store).value();
        return astro::core::Result<void>::success();
    }

    astro::core::Result<void> saveCampaignProgress()
    {
        if (!campaignSession_ || !campaignSaveStore_ || !campaignSaveProfile_)
            return astro::core::Result<void>::failure({astro::core::ErrorCode::InvalidState,
                "Campaign save slots are unavailable"});
        const auto saved = astro::runtime::savePuzzleCampaignProgress(*campaignSaveStore_, selectedSaveSlot_,
            scene_, *campaignSaveProfile_, "0.1.0", *campaignSession_);
        return saved ? astro::core::Result<void>::success()
            : astro::core::Result<void>::failure(saved.error());
    }

    astro::core::Result<void> loadCampaignProgress()
    {
        if (!campaignSession_ || !campaignSaveStore_ || !campaignSaveProfile_)
            return astro::core::Result<void>::failure({astro::core::ErrorCode::InvalidState,
                "Campaign save slots are unavailable"});
        const auto loaded = astro::runtime::loadPuzzleCampaignProgress(*campaignSaveStore_, selectedSaveSlot_,
            scene_, *campaignSaveProfile_, "0.1.0", campaignProjectRoot_, campaignId_);
        if (!loaded) return astro::core::Result<void>::failure(loaded.error());
        campaignSession_ = loaded.value().campaign;
        campaignLevelComplete_ = campaignSession_->completionPending();
        selectedPuzzleCell_.reset();
        pendingPuzzleMove_.reset();
        lastPuzzleMoveAccepted_ = false;
        return updatePuzzleHud();
    }

    astro::core::Result<void> resetActiveGame()
    {
        return campaignProjectRoot_.empty() ? resetPuzzle() : resetCampaign();
    }

    [[nodiscard]] const astro::runtime::PuzzleGame* activePuzzle() const noexcept
    {
        if (campaignSession_) return &campaignSession_->game();
        return puzzle_ ? &*puzzle_ : nullptr;
    }

    astro::core::Result<void> updatePuzzleHud()
    {
        const auto* game = activePuzzle();
        if (!game)
            return astro::core::Result<void>::success();
        const auto score = ui_.setHudText("hud.score", std::to_string(game->score()));
        if (!score) return score;
        const auto levelText = campaignSession_ ? campaignSession_->currentLevel().name
            : std::to_string(game->level());
        return ui_.setHudText("hud.level", levelText);
    }

    astro::core::Result<void> selectPuzzleCell(const astro::runtime::GridPosition position)
    {
        if (!activePuzzle())
            return astro::core::Result<void>::failure({astro::core::ErrorCode::InvalidState,
                "Puzzle board is not initialized"});
        if (pendingPuzzleMove_ || (campaignSession_ && (campaignLevelComplete_
            || campaignSession_->state() == astro::runtime::PuzzleCampaignSessionState::Ended)))
        {
            selectedPuzzleCell_.reset();
            return astro::core::Result<void>::success();
        }
        if (!selectedPuzzleCell_)
        {
            selectedPuzzleCell_ = position;
            adapter_.setPersistentDiagnostic({});
            return astro::core::Result<void>::success();
        }

        const auto first = *selectedPuzzleCell_;
        const auto columnDistance = first.column > position.column
            ? first.column - position.column : position.column - first.column;
        const auto rowDistance = first.row > position.row
            ? first.row - position.row : position.row - first.row;
        if (columnDistance + rowDistance != 1)
        {
            selectedPuzzleCell_ = position;
            return astro::core::Result<void>::success();
        }
        selectedPuzzleCell_.reset();
        pendingPuzzleMove_ = std::pair{first, position};
        return astro::core::Result<void>::success();
    }

    astro::core::Result<void> applyPendingPuzzleMove()
    {
        if (!pendingPuzzleMove_) return astro::core::Result<void>::success();
        const auto [first, second] = *pendingPuzzleMove_;
        pendingPuzzleMove_.reset();
        const auto moved = campaignSession_
            ? campaignSession_->swap(first, second) : puzzle_->swap(first, second);
        if (!moved) return astro::core::Result<void>::failure(moved.error());
        lastPuzzleMoveAccepted_ = moved.value().accepted;
        if (campaignSession_) campaignLevelComplete_ = moved.value().levelCompleted;
        if (lastPuzzleMoveAccepted_ && !smoke_) (void)audio_.play(astro::runtime::AudioCue::Match);
        return updatePuzzleHud();
    }

    astro::core::Result<void> continueCampaign()
    {
        if (!campaignSession_)
            return astro::core::Result<void>::failure({astro::core::ErrorCode::InvalidState,
                "No Puzzle campaign is active"});
        const auto advanced = campaignSession_->completeCurrentLevel();
        if (!advanced) return astro::core::Result<void>::failure(advanced.error());
        campaignLevelComplete_ = false;
        selectedPuzzleCell_.reset();
        lastPuzzleMoveAccepted_ = false;
        adapter_.setPersistentDiagnostic({});
        if (!smoke_) (void)audio_.play(astro::runtime::AudioCue::LevelUp);
        return updatePuzzleHud();
    }

    astro::core::Result<void> drawPuzzleBoard()
    {
        const auto* game = activePuzzle();
        if (!game)
            return astro::core::Result<void>::failure({astro::core::ErrorCode::InvalidState,
                "Puzzle board is not initialized"});
        ++puzzleBoardRenderedFrames_;
        const auto setting = ui_.settings().find("ui.language");
        const auto* languageCode = setting == ui_.settings().end()
            ? nullptr : std::get_if<std::string>(&setting->second.value);
        const auto language = languageCode
            ? astro::runtime::gameUiLanguageFromCode(*languageCode)
            : astro::runtime::GameUiLanguage::English;
        const auto instruction = astro::runtime::translateGameUiText("Select two adjacent tiles", language);
        ImGui::TextWrapped("%s", instruction.c_str());

        if (campaignSession_)
        {
            const auto campaignLabel = astro::runtime::translateGameUiText("Campaign", language);
            ImGui::Text("%s: %s / %s", campaignLabel.c_str(), campaignSession_->campaign().id.c_str(),
                campaignSession_->currentLevel().id.c_str());
            if (campaignSession_->state() == astro::runtime::PuzzleCampaignSessionState::Ended)
            {
                const auto finished = astro::runtime::translateGameUiText("Campaign complete", language);
                ImGui::TextWrapped("%s", finished.c_str());
            }
            else if (campaignLevelComplete_)
            {
                const auto completed = astro::runtime::translateGameUiText("Level complete", language);
                ImGui::TextWrapped("%s", completed.c_str());
            }
            const auto slotLabel = astro::runtime::translateGameUiText("Save slot", language);
            ImGui::TextUnformatted(slotLabel.c_str());
            ImGui::SameLine();
            ImGui::SetNextItemWidth(92.0f);
            const auto slotId = "##campaign-save-slot";
            if (ImGui::BeginCombo(slotId, std::to_string(selectedSaveSlot_).c_str()))
            {
                for (std::size_t slot = 0; slot < 10; ++slot)
                {
                    const auto slotText = std::to_string(slot);
                    const bool selected = selectedSaveSlot_ == slot;
                    if (ImGui::Selectable(slotText.c_str(), selected)) selectedSaveSlot_ = slot;
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            const auto saveLabel = astro::runtime::translateGameUiText("Save progress", language);
            ImGui::SameLine();
            if (ImGui::Button(saveLabel.c_str()))
            {
                const auto saved = saveCampaignProgress();
                adapter_.setPersistentDiagnostic(saved ? std::string{} : saved.error().message);
            }
            const auto loadLabel = astro::runtime::translateGameUiText("Load progress", language);
            ImGui::SameLine();
            if (ImGui::Button(loadLabel.c_str()))
            {
                const auto loaded = loadCampaignProgress();
                adapter_.setPersistentDiagnostic(loaded ? std::string{} : loaded.error().message);
            }
        }

        constexpr std::array<ImVec4, 5> tileColors{{
            {0.18f, 0.46f, 0.82f, 1.0f}, {0.22f, 0.64f, 0.40f, 1.0f},
            {0.89f, 0.48f, 0.19f, 1.0f}, {0.62f, 0.35f, 0.78f, 1.0f},
            {0.85f, 0.72f, 0.22f, 1.0f},
        }};
        const auto width = game->width();
        const auto height = game->height();
        if (width == 0 || height == 0 || width > 16 || height > 16)
            return astro::core::Result<void>::failure({astro::core::ErrorCode::ValidationError,
                "Puzzle board dimensions exceed the Game UI tile limit"});
        const auto availableWidth = ImGui::GetContentRegionAvail().x;
        const auto cellSize = std::clamp((availableWidth - 2.0f * static_cast<float>(width - 1))
            / static_cast<float>(width), 22.0f, 44.0f);
        const auto boardWidth = static_cast<float>(width) * cellSize
            + static_cast<float>(width - 1) * 2.0f;
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(),
            ImGui::GetCursorPosX() + (availableWidth - boardWidth) * 0.5f));
        for (std::size_t row = 0; row < height; ++row)
        {
            for (std::size_t column = 0; column < width; ++column)
            {
                if (column != 0) ImGui::SameLine(0.0f, 2.0f);
                const astro::runtime::GridPosition position{column, row};
                const auto tile = game->tileAt(position);
                if (tile >= tileColors.size())
                    return astro::core::Result<void>::failure({astro::core::ErrorCode::ValidationError,
                        "Puzzle tile is outside the display palette"});
                const bool selected = selectedPuzzleCell_ && *selectedPuzzleCell_ == position;
                auto color = tileColors[tile];
                if (selected) color = ImVec4{0.95f, 0.95f, 0.95f, 1.0f};
                const auto label = std::to_string(static_cast<unsigned int>(tile) + 1)
                    + "##puzzle-" + std::to_string(row) + "-" + std::to_string(column);
                ImGui::PushStyleColor(ImGuiCol_Button, color);
                if (ImGui::Button(label.c_str(), ImVec2(cellSize, cellSize)))
                {
                    const auto selectedResult = selectPuzzleCell(position);
                    if (!selectedResult) return selectedResult;
                }
                ImGui::PopStyleColor();
            }
        }
        if (campaignSession_ && campaignLevelComplete_
            && campaignSession_->state() == astro::runtime::PuzzleCampaignSessionState::Running)
        {
            const auto continueLabel = astro::runtime::translateGameUiText("Continue campaign", language);
            if (ImGui::Button(continueLabel.c_str()))
            {
                const auto advanced = continueCampaign();
                if (!advanced) return advanced;
            }
        }
        return astro::core::Result<void>::success();
    }

    std::pair<astro::runtime::GridPosition, astro::runtime::GridPosition> findAcceptedMove() const
    {
        const auto* game = activePuzzle();
        if (!game) throw std::runtime_error("Puzzle board is not initialized");
        for (std::size_t row = 0; row < game->height(); ++row)
            for (std::size_t column = 0; column < game->width(); ++column)
                for (const auto offset : {astro::runtime::GridPosition{1, 0}, astro::runtime::GridPosition{0, 1}})
                {
                    const astro::runtime::GridPosition next{column + offset.column, row + offset.row};
                    if (next.column >= game->width() || next.row >= game->height()) continue;
                    auto candidate = *game;
                    const auto move = candidate.swap({column, row}, next);
                    if (move && move.value().accepted) return {{column, row}, next};
                }
        throw std::runtime_error("Seeded Puzzle board has no accepted adjacent move");
    }

    astro::core::Result<void> startRuntime()
    {
        return ui_.startGame(session_, scene_, [this](astro::scene::Scene&, const double) {
            return applyPendingPuzzleMove();
        }, events_);
    }

    int runPuzzleSmoke()
    {
        const auto started = startRuntime();
        if (!started) throw std::runtime_error(started.error().message);
        const auto acceptedMove = findAcceptedMove();
        const auto scoreBeforeInvalidSelection = activePuzzle()->score();
        if (!selectPuzzleCell({0, 0}) || !selectPuzzleCell({7, 7})
            || activePuzzle()->score() != scoreBeforeInvalidSelection)
            throw std::runtime_error("Non-adjacent Puzzle tile selection changed the board");
        selectedPuzzleCell_.reset();
        const auto firstSelection = selectPuzzleCell(acceptedMove.first);
        const auto secondSelection = selectPuzzleCell(acceptedMove.second);
        const auto tickedMove = session_.tick(1.0 / 60.0);
        if (!firstSelection || !secondSelection || !tickedMove || !lastPuzzleMoveAccepted_ || !activePuzzle()
            || activePuzzle()->score() == 0)
            throw std::runtime_error("Puzzle Game UI input did not apply the deterministic valid move");
        const auto gameplayRender = adapter_.render();
        if (!gameplayRender || adapter_.lastRenderedVertexCount() == 0 || puzzleBoardRenderedFrames_ == 0)
            throw std::runtime_error("Puzzle Game UI did not render the board in the gameplay screen");
        const auto score = ui_.hudFields().find("hud.score");
        if (score == ui_.hudFields().end() || score->second.text != std::to_string(activePuzzle()->score()))
            throw std::runtime_error("Puzzle Game UI HUD did not reflect the accepted move");
        const auto paused = ui_.pause(session_, events_);
        if (!paused || ui_.screen() != astro::runtime::GameUiScreen::Paused
            || session_.state() != astro::runtime::RuntimeState::Paused)
            throw std::runtime_error("Puzzle Game UI pause did not pause the runtime session");
        if (!adapter_.render()) throw std::runtime_error("Puzzle pause screen did not render");
        const auto resumed = ui_.resume(session_, events_);
        if (!resumed || ui_.screen() != astro::runtime::GameUiScreen::Gameplay)
            throw std::runtime_error("Puzzle Game UI resume did not restore gameplay");
        if (!adapter_.render() || puzzleBoardRenderedFrames_ < 2)
            throw std::runtime_error("Puzzle board was not rendered again after resume");
        const auto returned = ui_.returnToMenu(session_, events_);
        if (!returned || ui_.screen() != astro::runtime::GameUiScreen::MainMenu
            || session_.state() != astro::runtime::RuntimeState::Stopped
            || !scene_.find(1) || scene_.find(1)->name != "Demo player")
            throw std::runtime_error("Puzzle Game UI return-to-menu did not stop the isolated runtime");
        std::cout << "[game-ui-puzzle-smoke] move=accepted score=" << activePuzzle()->score()
                  << " boardFrames=" << puzzleBoardRenderedFrames_ << " pause=resume=menu=ok\n";
        return 0;
    }

    int runCampaignSmoke()
    {
        const auto started = startRuntime();
        if (!started) throw std::runtime_error(started.error().message);
        const auto playOneLevel = [this]() {
            const auto move = findAcceptedMove();
            const auto scoreBeforeTick = activePuzzle()->score();
            const auto first = selectPuzzleCell(move.first);
            const auto second = selectPuzzleCell(move.second);
            if (!first || !second || activePuzzle()->score() != scoreBeforeTick) return false;
            const auto ticked = session_.tick(1.0 / 60.0);
            return ticked && lastPuzzleMoveAccepted_ && campaignLevelComplete_;
        };
        if (!campaignSession_ || campaignSession_->currentLevel().id != "intro" || !playOneLevel())
            throw std::runtime_error("Campaign smoke could not complete the authored START level");
        if (!adapter_.render() || adapter_.lastRenderedVertexCount() == 0)
            throw std::runtime_error("Completed campaign level did not render its explicit Continue action");
        const auto savedIntro = saveCampaignProgress();
        if (!savedIntro || !campaignSaveStore_->inspect(selectedSaveSlot_))
            throw std::runtime_error("Campaign smoke could not persist the selected progress slot");
        const auto introScore = campaignSession_->game().score();
        const auto firstAdvance = continueCampaign();
        if (!firstAdvance || campaignSession_->currentLevel().id != "finale"
            || campaignSession_->game().score() != 0 || campaignLevelComplete_)
            throw std::runtime_error("Campaign smoke did not load the threshold-selected authored level");
        const auto restoredIntro = loadCampaignProgress();
        if (!restoredIntro || campaignSession_->currentLevel().id != "intro"
            || campaignSession_->game().score() != introScore || !campaignLevelComplete_)
            throw std::runtime_error("Campaign smoke did not restore saved pending intro progress");
        const auto resumedAdvance = continueCampaign();
        if (!resumedAdvance || campaignSession_->currentLevel().id != "finale")
            throw std::runtime_error("Campaign smoke did not continue from restored progress");
        if (!playOneLevel())
            throw std::runtime_error("Campaign smoke could not complete the authored final level");
        const auto finalScore = campaignSession_->game().score();
        if (!adapter_.render()) throw std::runtime_error("Completed final campaign level did not render");
        const auto end = continueCampaign();
        if (!end || campaignSession_->state() != astro::runtime::PuzzleCampaignSessionState::Ended
            || campaignSession_->currentLevel().id != "finale" || campaignSession_->game().score() != finalScore)
            throw std::runtime_error("Campaign END did not retain the final authored board and score");
        const auto savedEnd = saveCampaignProgress();
        if (!savedEnd) throw std::runtime_error("Campaign smoke could not persist its END state");
        const auto restoredEnd = loadCampaignProgress();
        if (!restoredEnd || campaignSession_->state() != astro::runtime::PuzzleCampaignSessionState::Ended
            || campaignSession_->currentLevel().id != "finale"
            || campaignSession_->game().score() != finalScore)
            throw std::runtime_error("Campaign smoke did not restore the persisted END state");
        if (!adapter_.render()) throw std::runtime_error("Campaign END results state did not render");
        const auto returned = ui_.returnToMenu(session_, events_);
        if (!returned || session_.state() != astro::runtime::RuntimeState::Stopped
            || !scene_.find(1) || scene_.find(1)->name != "Demo player")
            throw std::runtime_error("Campaign stop mutated the authoring Scene or failed to stop runtime");
        std::cout << "[game-ui-campaign-smoke] save=restore-pending end=save-restore score="
                  << finalScore << " ok\n";
        return 0;
    }

    static std::vector<std::uint8_t> rgbaPixels(SDL_Surface* source)
    {
        Surface converted(SDL_ConvertSurface(source, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
        if (!converted) throw std::runtime_error(SDL_GetError());
        if (converted->w <= 0 || converted->h <= 0 || converted->w > 4096 || converted->h > 4096)
            throw std::runtime_error("Visual reference has invalid dimensions");
        if (SDL_MUSTLOCK(converted.get()) && !SDL_LockSurface(converted.get()))
            throw std::runtime_error(SDL_GetError());
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(converted->w)
            * static_cast<std::size_t>(converted->h) * 4);
        const auto* sourceBytes = static_cast<const std::uint8_t*>(converted->pixels);
        const auto rowBytes = static_cast<std::size_t>(converted->w) * 4;
        for (int row = 0; row < converted->h; ++row)
            std::copy_n(sourceBytes + static_cast<std::size_t>(row) * converted->pitch,
                rowBytes, pixels.data() + static_cast<std::size_t>(row) * rowBytes);
        if (SDL_MUSTLOCK(converted.get())) SDL_UnlockSurface(converted.get());
        return pixels;
    }

    void compareOrWriteGolden()
    {
        Surface rendered(SDL_RenderReadPixels(renderer_, nullptr), SDL_DestroySurface);
        if (!rendered) throw std::runtime_error(std::string("Could not capture Game UI frame: ") + SDL_GetError());
        if (updateGolden_)
        {
            if (!SDL_SaveBMP(rendered.get(), goldenPath_.c_str()))
                throw std::runtime_error(std::string("Could not write visual reference: ") + SDL_GetError());
            return;
        }
        Surface golden(SDL_LoadBMP(goldenPath_.c_str()), SDL_DestroySurface);
        if (!golden) throw std::runtime_error(std::string("Could not read visual reference: ") + SDL_GetError());
        if (rendered->w != golden->w || rendered->h != golden->h)
            throw std::runtime_error("Game UI visual reference dimensions changed");
        const auto actual = rgbaPixels(rendered.get());
        const auto reference = rgbaPixels(golden.get());
        std::size_t pixelsOverChannelTolerance = 0;
        std::uint64_t totalAbsoluteError = 0;
        const auto pixelCount = actual.size() / 4;
        for (std::size_t pixel = 0; pixel < pixelCount; ++pixel)
        {
            bool differs = false;
            for (std::size_t channel = 0; channel < 4; ++channel)
            {
                const auto a = static_cast<int>(actual[pixel * 4 + channel]);
                const auto b = static_cast<int>(reference[pixel * 4 + channel]);
                const auto error = static_cast<std::uint64_t>(std::abs(a - b));
                totalAbsoluteError += error;
                if (error > 12) differs = true;
            }
            if (differs) ++pixelsOverChannelTolerance;
        }
        const auto channelCount = actual.size();
        const auto meanAbsoluteError = static_cast<double>(totalAbsoluteError) / static_cast<double>(channelCount);
        const auto changedRatio = static_cast<double>(pixelsOverChannelTolerance) / static_cast<double>(pixelCount);
        if (meanAbsoluteError > 1.5 || changedRatio > 0.01)
            throw std::runtime_error("Game UI main-menu screenshot exceeded tolerance: mean MAE="
                + std::to_string(meanAbsoluteError) + ", changed pixels=" + std::to_string(changedRatio));
    }

    int runSmoke()
    {
        std::size_t renderedScreens = 0;
        const auto render = [&]() {
            const auto result = adapter_.render();
            if (!result || adapter_.lastRenderedVertexCount() == 0)
                throw std::runtime_error(result ? "Game UI produced no render vertices" : result.error().message);
            ++renderedScreens;
        };
        render(); // main menu
        if (!goldenPath_.empty()) compareOrWriteGolden();
        const auto started = ui_.startGame(session_, scene_,
            [this](astro::scene::Scene&, const double) {
                ++score_;
                return ui_.setHudText("hud.score", std::to_string(score_));
            }, events_);
        if (!started) throw std::runtime_error(started.error().message);
        render(); // HUD during gameplay
        const auto paused = ui_.pause(session_, events_);
        if (!paused) throw std::runtime_error(paused.error().message);
        render(); // pause menu
        const auto opened = ui_.openSettings(events_);
        if (!opened) throw std::runtime_error(opened.error().message);
        const auto changed = ui_.setSetting("ui.scale", 1.2, events_);
        if (!changed) throw std::runtime_error(changed.error().message);
        render(); // settings panel
        const auto switchedToRussian = ui_.setSetting("ui.language", std::string{"ru"}, events_);
        if (!switchedToRussian) throw std::runtime_error(switchedToRussian.error().message);
        const auto persistedRussian = astro::runtime::readGameUiLanguagePreference(languagePreferencePath_);
        if (!persistedRussian || persistedRussian.value() != astro::runtime::GameUiLanguage::Russian)
            throw std::runtime_error("Russian Game UI preference was not persisted by the settings flow");
        render(); // Russian settings panel
        const auto switchedBackToEnglish = ui_.setSetting("ui.language", std::string{"en"}, events_);
        if (!switchedBackToEnglish) throw std::runtime_error(switchedBackToEnglish.error().message);
        const auto persistedEnglish = astro::runtime::readGameUiLanguagePreference(languagePreferencePath_);
        if (!persistedEnglish || persistedEnglish.value() != astro::runtime::GameUiLanguage::English)
            throw std::runtime_error("English Game UI preference was not persisted by the settings flow");
        render();
        const auto closed = ui_.closeSettings(events_);
        const auto resumed = ui_.resume(session_, events_);
        if (!closed || !resumed) throw std::runtime_error("Failed to resume after Settings");
        render();
        const auto menu = ui_.returnToMenu(session_, events_);
        if (!menu) throw std::runtime_error(menu.error().message);
        render();
        return renderedScreens == 8 && ui_.screen() == astro::runtime::GameUiScreen::MainMenu
            && session_.state() == astro::runtime::RuntimeState::Stopped ? 0 : 1;
    }

    bool smoke_;
    bool puzzleSmoke_;
    bool campaignSmoke_;
    std::filesystem::path campaignProjectRoot_;
    std::string campaignId_;
    bool updateGolden_;
    std::string goldenPath_;
    SDL_Window* window_;
    SDL_Renderer* renderer_;
    astro::runtime::GameUiModel ui_;
    astro::runtime::RuntimeSession session_;
    astro::core::EventBus events_;
    astro::scene::Scene scene_;
    astro::runtime::SdlAudioCuePlayer audio_;
    int score_ = 0;
    std::optional<astro::runtime::PuzzleGame> puzzle_;
    std::optional<astro::runtime::PuzzleCampaignSession> campaignSession_;
    std::optional<astro::runtime::SaveStore> campaignSaveStore_;
    std::optional<astro::runtime::SaveProfile> campaignSaveProfile_;
    std::filesystem::path userDataRoot_;
    std::size_t selectedSaveSlot_ = 0;
    bool campaignLevelComplete_ = false;
    std::optional<std::pair<astro::runtime::GridPosition, astro::runtime::GridPosition>> pendingPuzzleMove_;
    std::optional<astro::runtime::GridPosition> selectedPuzzleCell_;
    bool lastPuzzleMoveAccepted_ = false;
    std::size_t puzzleBoardRenderedFrames_ = 0;
    std::filesystem::path languagePreferencePath_;
    astro::runtime::ImGuiGameUiAdapter adapter_;
};
}

int runApplication(const std::vector<std::string>& arguments)
{
    const auto argument = [&](const int index) -> std::string_view {
        return arguments[static_cast<std::size_t>(index)];
    };
    bool puzzleSmoke = false;
    bool campaignSmoke = false;
    bool smoke = false;
    bool updateGolden = false;
    std::filesystem::path campaignProjectRoot;
    std::string campaignId;
    std::string goldenPath;
    if (arguments.empty())
    {
    }
    else if (arguments.size() == 1 && argument(0) == "--smoke-puzzle")
    {
        smoke = true;
        puzzleSmoke = true;
    }
    else if (arguments.size() == 4 && argument(0) == "--project-root" && argument(2) == "--campaign")
    {
        campaignProjectRoot = pathFromUtf8(argument(1));
        campaignId = arguments[3];
    }
    else if (arguments.size() == 5 && argument(0) == "--project-root"
        && argument(2) == "--campaign" && argument(4) == "--smoke")
    {
        smoke = true;
        campaignSmoke = true;
        campaignProjectRoot = pathFromUtf8(argument(1));
        campaignId = arguments[3];
    }
    else if (arguments.size() == 1 && argument(0) == "--smoke")
    {
        smoke = true;
    }
    else if (arguments.size() == 2 && argument(0) == "--smoke")
    {
        smoke = true;
        goldenPath = arguments[1];
    }
    else if (arguments.size() == 3 && argument(0) == "--smoke" && argument(1) == "--update-golden")
    {
        smoke = true;
        updateGolden = true;
        goldenPath = arguments[2];
    }
    else
    {
        std::cerr << "Usage: astro_game_ui_demo [--smoke [golden.bmp | --update-golden path] | --smoke-puzzle"
            " | --project-root <path> --campaign <id> [--smoke]]\n";
        return 2;
    }
    try
    {
        SdlLifetime sdl;
        const auto flags = SDL_WINDOW_RESIZABLE | (smoke ? SDL_WINDOW_HIDDEN : 0);
        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
            SDL_CreateWindow("AstroEngine — Game UI prototype", 960, 640, flags), SDL_DestroyWindow);
        if (!window) throw std::runtime_error(SDL_GetError());
        std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer(
            SDL_CreateRenderer(window.get(), smoke ? "software" : nullptr), SDL_DestroyRenderer);
        if (!renderer) throw std::runtime_error(SDL_GetError());
        Demo demo(smoke, puzzleSmoke, campaignSmoke, std::move(campaignProjectRoot), std::move(campaignId),
            updateGolden, std::move(goldenPath), window.get(), renderer.get());
        return demo.run();
    }
    catch (const std::exception& error)
    {
        std::cerr << "[error] " << error.what() << '\n';
        return 1;
    }
}

#ifdef _WIN32
int wmain(const int argc, wchar_t** argv)
{
    std::vector<std::string> arguments;
    arguments.reserve(static_cast<std::size_t>(argc - 1));
    for (int index = 1; index < argc; ++index)
    {
        const auto required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[index], -1,
            nullptr, 0, nullptr, nullptr);
        if (required <= 1)
        {
            std::cerr << "[error] Command-line argument is not valid Unicode\n";
            return 2;
        }
        std::string utf8(static_cast<std::size_t>(required), '\0');
        if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[index], -1,
                utf8.data(), required, nullptr, nullptr) != required)
        {
            std::cerr << "[error] Could not convert command-line argument to UTF-8\n";
            return 2;
        }
        utf8.pop_back();
        arguments.push_back(std::move(utf8));
    }
    return runApplication(arguments);
}
#else
int main(const int argc, char** argv)
{
    std::vector<std::string> arguments;
    arguments.reserve(static_cast<std::size_t>(argc - 1));
    for (int index = 1; index < argc; ++index) arguments.emplace_back(argv[index]);
    return runApplication(arguments);
}
#endif

#pragma once

#include "astro/core/Result.hpp"

#include <SDL3/SDL.h>

namespace astro::runtime
{
enum class AudioCue { Select, Match, LevelUp };

// Optional SDL3 tone cue output. Requires SDL_INIT_AUDIO and a live audio device.
class SdlAudioCuePlayer final
{
public:
    SdlAudioCuePlayer() = default;
    ~SdlAudioCuePlayer();
    SdlAudioCuePlayer(const SdlAudioCuePlayer&) = delete;
    SdlAudioCuePlayer& operator=(const SdlAudioCuePlayer&) = delete;
    core::Result<void> open();
    core::Result<void> play(AudioCue cue);
    [[nodiscard]] bool isOpen() const noexcept { return stream_ != nullptr; }
private:
    SDL_AudioStream* stream_ = nullptr;
};
}

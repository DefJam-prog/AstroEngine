#include "astro/runtime/SdlAudioCuePlayer.hpp"

#include <cmath>
#include <algorithm>
#include <numbers>
#include <string>
#include <vector>

namespace astro::runtime
{
SdlAudioCuePlayer::~SdlAudioCuePlayer()
{
    if (stream_) SDL_DestroyAudioStream(stream_);
}

core::Result<void> SdlAudioCuePlayer::open()
{
    if (stream_) return core::Result<void>::success();
    const SDL_AudioSpec format{SDL_AUDIO_F32, 1, 48000};
    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &format, nullptr, nullptr);
    if (!stream_)
        return core::Result<void>::failure({core::ErrorCode::IoError,
            std::string("Could not open SDL audio output: ") + SDL_GetError()});
    if (!SDL_ResumeAudioStreamDevice(stream_))
    {
        const std::string message = std::string("Could not start SDL audio output: ") + SDL_GetError();
        SDL_DestroyAudioStream(stream_);
        stream_ = nullptr;
        return core::Result<void>::failure({core::ErrorCode::IoError, message});
    }
    return core::Result<void>::success();
}

core::Result<void> SdlAudioCuePlayer::play(const AudioCue cue)
{
    if (!stream_)
        return core::Result<void>::failure({core::ErrorCode::InvalidState, "SDL audio output is not open"});
    double frequency = 440.0;
    switch (cue)
    {
    case AudioCue::Select: frequency = 523.25; break;
    case AudioCue::Match: frequency = 659.25; break;
    case AudioCue::LevelUp: frequency = 880.0; break;
    }
    constexpr int sampleRate = 48000;
    constexpr int frameCount = 3600;
    std::vector<float> samples(frameCount);
    for (int frame = 0; frame < frameCount; ++frame)
    {
        const auto attack = std::min(1.0, static_cast<double>(frame) / 240.0);
        const auto release = std::min(1.0, static_cast<double>(frameCount - frame) / 720.0);
        const auto envelope = attack * release;
        samples[static_cast<std::size_t>(frame)] = static_cast<float>(0.16 * envelope
            * std::sin(2.0 * std::numbers::pi * frequency * frame / sampleRate));
    }
    if (!SDL_PutAudioStreamData(stream_, samples.data(), static_cast<int>(samples.size() * sizeof(float))))
        return core::Result<void>::failure({core::ErrorCode::IoError,
            std::string("Could not queue SDL audio cue: ") + SDL_GetError()});
    return core::Result<void>::success();
}
}

#include "astro/runtime/SdlAudioCuePlayer.hpp"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <iostream>

int main()
{
    if (!SDL_Init(SDL_INIT_AUDIO))
    {
        std::cerr << "SDL audio init failed: " << SDL_GetError() << '\n';
        return EXIT_FAILURE;
    }
    int result = EXIT_SUCCESS;
    {
        astro::runtime::SdlAudioCuePlayer player;
        const auto opened = player.open();
        if (!opened)
        {
            std::cerr << opened.error().message << '\n';
            result = EXIT_FAILURE;
        }
        else
        {
            const auto played = player.play(astro::runtime::AudioCue::Match);
            if (!played) { std::cerr << played.error().message << '\n'; result = EXIT_FAILURE; }
        }
    }
    SDL_Quit();
    return result;
}

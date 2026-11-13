#pragma once

#include "astro/runtime/InputMapping.hpp"

#include <SDL3/SDL_events.h>

namespace astro::runtime
{
// Converts SDL3 event-queue records to backend-independent physical input state.
class SDLInputAdapter final
{
public:
    explicit SDLInputAdapter(InputMapping& mapping) noexcept : mapping_(mapping) {}
    core::Result<bool> processEvent(const SDL_Event& event);

private:
    InputMapping& mapping_;
};
}

#include "astro/runtime/SDLInputAdapter.hpp"

#include <cmath>

namespace astro::runtime
{
namespace
{
InputControl control(InputDevice device, int code, std::int64_t deviceId = 0)
{
    return {device, code, deviceId};
}
core::Result<bool> handled(core::Result<void> result)
{
    if (!result) return core::Result<bool>::failure(result.error());
    return core::Result<bool>::success(true);
}
}

core::Result<bool> SDLInputAdapter::processEvent(const SDL_Event& event)
{
    switch (event.type)
    {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        if (event.key.scancode == SDL_SCANCODE_UNKNOWN) return core::Result<bool>::success(false);
        return handled(mapping_.setDigital(control(InputDevice::KeyboardKey, event.key.scancode), event.key.down));
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        return handled(mapping_.setDigital(control(InputDevice::MouseButton, event.button.button,
            static_cast<std::int64_t>(event.button.which)), event.button.down));
    case SDL_EVENT_MOUSE_MOTION:
    {
        const auto deviceId = static_cast<std::int64_t>(event.motion.which);
        auto horizontal = mapping_.addTransient(control(InputDevice::MouseDelta, 0, deviceId), event.motion.xrel);
        if (!horizontal) return core::Result<bool>::failure(horizontal.error());
        return handled(mapping_.addTransient(control(InputDevice::MouseDelta, 1, deviceId), event.motion.yrel));
    }
    case SDL_EVENT_MOUSE_WHEEL:
    {
        const float direction = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f;
        auto horizontal = mapping_.addTransient(control(InputDevice::MouseWheel, 0,
            static_cast<std::int64_t>(event.wheel.which)), event.wheel.x * direction);
        if (!horizontal) return core::Result<bool>::failure(horizontal.error());
        return handled(mapping_.addTransient(control(InputDevice::MouseWheel, 1,
            static_cast<std::int64_t>(event.wheel.which)), event.wheel.y * direction));
    }
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
        return handled(mapping_.setDigital(control(InputDevice::GamepadButton, event.gbutton.button,
            static_cast<std::int64_t>(event.gbutton.which)), event.gbutton.down));
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
    {
        const auto raw = static_cast<float>(event.gaxis.value);
        const auto normalized = raw < 0.0f ? raw / 32768.0f : raw / 32767.0f;
        return handled(mapping_.setAxis(control(InputDevice::GamepadAxis, event.gaxis.axis,
            static_cast<std::int64_t>(event.gaxis.which)), normalized));
    }
    case SDL_EVENT_GAMEPAD_REMOVED:
        mapping_.clearDevice(InputDevice::GamepadButton, static_cast<std::int64_t>(event.gdevice.which));
        mapping_.clearDevice(InputDevice::GamepadAxis, static_cast<std::int64_t>(event.gdevice.which));
        return core::Result<bool>::success(true);
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        mapping_.clearDevice(InputDevice::KeyboardKey, 0);
        mapping_.clearDevice(InputDevice::MouseButton, 0);
        mapping_.clearDevice(InputDevice::MouseDelta, 0);
        mapping_.clearDevice(InputDevice::MouseWheel, 0);
        return core::Result<bool>::success(true);
    default:
        return core::Result<bool>::success(false);
    }
}
}

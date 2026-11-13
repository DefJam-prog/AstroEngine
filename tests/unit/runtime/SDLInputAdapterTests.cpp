#include "astro/runtime/SDLInputAdapter.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace
{
int failures = 0;
void check(bool condition, const char* message)
{
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
}

int main()
{
    using namespace astro::runtime;
    InputMapping mapping;
    SDLInputAdapter adapter(mapping);
    check(static_cast<bool>(mapping.bind("jump", {{InputDevice::KeyboardKey, SDL_SCANCODE_SPACE}, 1.0f, 0.0f})),
        "bind keyboard action");
    check(static_cast<bool>(mapping.bind("fire", {{InputDevice::MouseButton, SDL_BUTTON_LEFT}, 1.0f, 0.0f})),
        "bind mouse button action");
    check(static_cast<bool>(mapping.bind("look_x", {{InputDevice::MouseDelta, 0}, 0.01f, 0.0f})),
        "bind mouse delta action");
    check(static_cast<bool>(mapping.bind("wheel", {{InputDevice::MouseWheel, 1}, 0.25f, 0.0f})),
        "bind mouse wheel action");
    check(static_cast<bool>(mapping.bind("fire", {{InputDevice::GamepadButton, SDL_GAMEPAD_BUTTON_SOUTH}, 1.0f, 0.0f})),
        "bind gamepad button action");
    check(static_cast<bool>(mapping.bind("move_x", {{InputDevice::GamepadAxis, SDL_GAMEPAD_AXIS_LEFTX}, 1.0f, 0.15f})),
        "bind gamepad axis action");

    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = SDL_SCANCODE_SPACE;
    event.key.down = true;
    check(adapter.processEvent(event) && mapping.state("jump").pressed, "adapt SDL key down to action press");
    event.type = SDL_EVENT_KEY_UP;
    event.key.down = false;
    check(adapter.processEvent(event) && mapping.state("jump").released, "adapt SDL key up to action release");

    event = {};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = SDL_SCANCODE_SPACE;
    event.key.down = true;
    check(adapter.processEvent(event) && mapping.state("jump").down, "press key before focus loss");
    event = {};
    event.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    check(adapter.processEvent(event) && !mapping.state("jump").down,
        "clear held keyboard state when SDL window loses focus");

    event = {};
    event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.down = true;
    check(adapter.processEvent(event) && mapping.state("fire").down, "map SDL mouse button down");
    event.type = SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.down = false;
    check(adapter.processEvent(event) && !mapping.state("fire").down, "map SDL mouse button up");

    event = {};
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.xrel = 12.0f;
    check(adapter.processEvent(event) && std::abs(mapping.state("look_x").value - 0.12f) < 0.001f,
        "map relative SDL mouse motion");
    event = {};
    event.type = SDL_EVENT_MOUSE_WHEEL;
    event.wheel.y = 2.0f;
    event.wheel.direction = SDL_MOUSEWHEEL_FLIPPED;
    check(adapter.processEvent(event) && std::abs(mapping.state("wheel").value + 0.5f) < 0.001f,
        "respect flipped SDL wheel direction");
    mapping.beginFrame();
    check(mapping.state("look_x").value == 0.0f && mapping.state("wheel").value == 0.0f,
        "clear transient mouse inputs at frame boundary");

    constexpr SDL_JoystickID gamepadId = 42;
    event = {};
    event.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    event.gbutton.which = gamepadId;
    event.gbutton.button = SDL_GAMEPAD_BUTTON_SOUTH;
    event.gbutton.down = true;
    check(adapter.processEvent(event) && mapping.state("fire").down, "map SDL gamepad button down");
    event = {};
    event.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
    event.gaxis.which = gamepadId;
    event.gaxis.axis = SDL_GAMEPAD_AXIS_LEFTX;
    event.gaxis.value = 16384;
    check(adapter.processEvent(event) && mapping.state("move_x").value > 0.49f
        && mapping.state("move_x").value < 0.51f, "normalize signed SDL gamepad axis");
    event = {};
    event.type = SDL_EVENT_GAMEPAD_REMOVED;
    event.gdevice.which = gamepadId;
    check(adapter.processEvent(event) && !mapping.state("fire").down
        && mapping.state("move_x").value == 0.0f, "clear disconnected gamepad state");

    event = {};
    event.type = SDL_EVENT_QUIT;
    const auto ignored = adapter.processEvent(event);
    check(ignored && !ignored.value(), "leave unrelated SDL events unhandled");
    if (failures == 0) std::cout << "All SDL input adapter tests passed.\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

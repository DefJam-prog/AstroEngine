#include "astro/runtime/InputMapping.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

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
    InputMapping input;
    const InputControl keyW{InputDevice::KeyboardKey, 26};
    const InputControl keyUp{InputDevice::KeyboardKey, 82};
    const InputControl keyS{InputDevice::KeyboardKey, 22};
    const InputControl padY{InputDevice::GamepadAxis, 1};
    const InputControl mouseX{InputDevice::MouseDelta, 0};
    check(static_cast<bool>(input.bind("move_forward", {keyW, 1.0f, 0.0f})), "bind keyboard key to action");
    check(static_cast<bool>(input.bind("move_forward", {keyUp, 1.0f, 0.0f})), "allow alternate binding");
    check(static_cast<bool>(input.bind("move_forward", {padY, -1.0f, 0.2f})), "bind signed gamepad axis and deadzone");
    check(static_cast<bool>(input.bind("move_horizontal", {keyS, -1.0f, 0.0f})), "bind signed digital axis");
    check(static_cast<bool>(input.bind("look_x", {mouseX, 0.01f, 0.0f})), "bind scaled mouse delta");
    check(!input.bind("bad action", {keyW, 1.0f, 0.0f})
        && !input.bind("move_forward", {{InputDevice::KeyboardKey, 0}, 1.0f, 0.0f})
        && !input.bind("move_forward", {keyW, std::numeric_limits<float>::infinity(), 0.0f}),
        "reject invalid action names, controls, and non-finite scales");
    check(input.actions().size() == 3, "enumerate action mappings deterministically");

    check(static_cast<bool>(input.setDigital(keyW, true)), "press keyboard source");
    auto state = input.state("move_forward");
    check(state.down && state.pressed && !state.released && state.value == 1.0f,
        "action state reports down and press edge");
    check(static_cast<bool>(input.setDigital(keyUp, true)), "press alternate source while action already held");
    check(!input.state("move_forward").pressed || input.state("move_forward").down,
        "alternate source keeps action active");
    input.beginFrame();
    check(!input.state("move_forward").pressed && input.state("move_forward").down,
        "frame boundary clears edge flags but retains held state");
    check(input.setDigital(keyW, false) && input.state("move_forward").down,
        "releasing one binding does not release action while another is held");
    check(static_cast<bool>(input.setDigital(keyUp, false)), "release final keyboard source");
    check(input.state("move_forward").released && !input.state("move_forward").down,
        "action reports release edge");

    check(static_cast<bool>(input.setAxis(padY, -0.1f)), "set gamepad axis inside deadzone");
    check(input.state("move_forward").value == 0.0f, "axis deadzone suppresses drift");
    check(static_cast<bool>(input.setAxis(padY, -0.75f)), "set gamepad axis outside deadzone");
    check(std::abs(input.state("move_forward").value - 0.75f) < 0.001f,
        "gamepad axis applies binding scale");
    check(!input.setAxis(padY, std::numeric_limits<float>::quiet_NaN()),
        "reject non-finite device values");
    input.clearDevice(InputDevice::GamepadAxis, 0);
    check(input.state("move_forward").value == 0.0f, "clear gamepad axis state");

    check(input.setDigital(keyS, true) && input.state("move_horizontal").value == -1.0f,
        "signed digital binding produces negative axis value");
    check(input.addTransient(mouseX, 40.0f) && std::abs(input.state("look_x").value - 0.4f) < 0.001f,
        "mouse delta accumulates and uses sensitivity scale");
    input.beginFrame();
    check(input.state("look_x").value == 0.0f, "transient mouse delta resets at next frame");
    check(input.state("unknown_action").value == 0.0f && !input.state("unknown_action").down,
        "unknown action safely resolves to neutral input");

    if (failures == 0) std::cout << "All input mapping unit tests passed.\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

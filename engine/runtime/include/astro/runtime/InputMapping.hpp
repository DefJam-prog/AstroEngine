#pragma once

#include "astro/core/Result.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace astro::runtime
{
enum class InputDevice : std::uint8_t { KeyboardKey, MouseButton, MouseDelta, MouseWheel, GamepadButton, GamepadAxis };

struct InputControl
{
    InputDevice device = InputDevice::KeyboardKey;
    std::int32_t code = 0;
    std::int64_t deviceId = 0; // 0 is a wildcard binding; event device IDs are otherwise preserved.
    friend auto operator<=>(const InputControl&, const InputControl&) = default;
};

struct InputBinding
{
    InputControl control;
    float scale = 1.0f;
    float deadzone = 0.0f;
    friend bool operator==(const InputBinding&, const InputBinding&) = default;
};

struct InputActionState
{
    float value = 0.0f; // Combined and clamped to [-1, 1].
    bool down = false;
    bool pressed = false;
    bool released = false;
};

// Backend-independent action map. Device adapters submit normalized physical
// state; callers delimit input frames with beginFrame().
class InputMapping final
{
public:
    core::Result<void> bind(std::string action, InputBinding binding);
    core::Result<void> unbind(const std::string& action, InputControl control);
    void beginFrame();
    core::Result<void> setDigital(InputControl control, bool down);
    core::Result<void> setAxis(InputControl control, float value);
    core::Result<void> addTransient(InputControl control, float value);
    void clearDevice(InputDevice device, std::int64_t deviceId);
    [[nodiscard]] InputActionState state(const std::string& action) const noexcept;
    [[nodiscard]] std::vector<std::string> actions() const;

private:
    [[nodiscard]] float actionValue(const std::string& action) const noexcept;
    void refreshEdges();
    std::map<std::string, std::vector<InputBinding>> bindings_;
    std::map<InputControl, float> values_;
    std::map<InputControl, float> transientValues_;
    std::map<std::string, InputActionState> states_;
};
}

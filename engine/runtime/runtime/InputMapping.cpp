#include "astro/runtime/InputMapping.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace astro::runtime
{
namespace
{
core::Error inputError(core::ErrorCode code, const char* message) { return {code, message}; }
bool validControl(const InputControl control)
{
    if (control.code < 0 || control.deviceId < 0) return false;
    switch (control.device)
    {
    case InputDevice::KeyboardKey: return control.code > 0 && control.code <= 512;
    case InputDevice::MouseButton: return control.code > 0 && control.code <= 32;
    case InputDevice::MouseDelta:
    case InputDevice::MouseWheel: return control.code <= 1;
    case InputDevice::GamepadButton: return control.code <= 63;
    case InputDevice::GamepadAxis: return control.code <= 15;
    }
    return false;
}
bool digitalDevice(const InputDevice device)
{
    return device == InputDevice::KeyboardKey || device == InputDevice::MouseButton
        || device == InputDevice::GamepadButton;
}
bool transientDevice(const InputDevice device)
{
    return device == InputDevice::MouseDelta || device == InputDevice::MouseWheel;
}
}

core::Result<void> InputMapping::bind(std::string action, InputBinding binding)
{
    if (action.empty() || action.size() > 128)
        return core::Result<void>::failure(inputError(core::ErrorCode::InvalidArgument, "Action name must contain 1 to 128 bytes"));
    for (const auto c : action)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-'))
            return core::Result<void>::failure(inputError(core::ErrorCode::InvalidArgument, "Action name contains an unsupported character"));
    if (!validControl(binding.control) || !std::isfinite(binding.scale) || binding.scale == 0.0f
        || std::abs(binding.scale) > 100.0f || !std::isfinite(binding.deadzone)
        || binding.deadzone < 0.0f || binding.deadzone >= 1.0f)
        return core::Result<void>::failure(inputError(core::ErrorCode::ValidationError, "Input binding control, scale, or deadzone is invalid"));
    auto& list = bindings_[action];
    if (list.size() >= 32) return core::Result<void>::failure(inputError(core::ErrorCode::ValidationError, "An action cannot have more than 32 bindings"));
    if (std::find_if(list.begin(), list.end(), [&binding](const auto& current) { return current.control == binding.control; }) != list.end())
        return core::Result<void>::failure(inputError(core::ErrorCode::AlreadyExists, "This control is already bound to the action"));
    list.push_back(binding);
    states_.try_emplace(std::move(action));
    refreshEdges();
    return core::Result<void>::success();
}

core::Result<void> InputMapping::unbind(const std::string& action, const InputControl control)
{
    const auto found = bindings_.find(action);
    if (found == bindings_.end()) return core::Result<void>::failure(inputError(core::ErrorCode::NotFound, "Input action does not exist"));
    auto& list = found->second;
    const auto priorSize = list.size();
    std::erase_if(list, [control](const auto& binding) { return binding.control == control; });
    if (priorSize == list.size()) return core::Result<void>::failure(inputError(core::ErrorCode::NotFound, "Input binding does not exist"));
    refreshEdges();
    return core::Result<void>::success();
}

void InputMapping::beginFrame()
{
    for (auto& [_, state] : states_) state.pressed = state.released = false;
    for (const auto& [control, _] : transientValues_) values_.erase(control);
    transientValues_.clear();
    refreshEdges();
}

core::Result<void> InputMapping::setDigital(const InputControl control, const bool down)
{
    if (!validControl(control) || !digitalDevice(control.device))
        return core::Result<void>::failure(inputError(core::ErrorCode::InvalidArgument, "setDigital requires a valid key or button control"));
    values_[control] = down ? 1.0f : 0.0f;
    refreshEdges();
    return core::Result<void>::success();
}

core::Result<void> InputMapping::setAxis(const InputControl control, const float value)
{
    if (!validControl(control) || control.device != InputDevice::GamepadAxis || !std::isfinite(value))
        return core::Result<void>::failure(inputError(core::ErrorCode::InvalidArgument, "setAxis requires a finite gamepad axis value"));
    values_[control] = std::clamp(value, -1.0f, 1.0f);
    refreshEdges();
    return core::Result<void>::success();
}

core::Result<void> InputMapping::addTransient(const InputControl control, const float value)
{
    if (!validControl(control) || !transientDevice(control.device) || !std::isfinite(value))
        return core::Result<void>::failure(inputError(core::ErrorCode::InvalidArgument, "addTransient requires a finite mouse delta/wheel value"));
    transientValues_[control] += value;
    transientValues_[control] = std::clamp(transientValues_[control], -100000.0f, 100000.0f);
    values_[control] = transientValues_[control];
    refreshEdges();
    return core::Result<void>::success();
}

void InputMapping::clearDevice(const InputDevice device, const std::int64_t deviceId)
{
    const auto matches = [device, deviceId](const auto& item) {
        return item.first.device == device && (deviceId == 0 || item.first.deviceId == deviceId);
    };
    std::erase_if(values_, matches);
    std::erase_if(transientValues_, matches);
    refreshEdges();
}

float InputMapping::actionValue(const std::string& action) const noexcept
{
    const auto found = bindings_.find(action);
    if (found == bindings_.end()) return 0.0f;
    float value = 0.0f;
    for (const auto& binding : found->second)
    {
        float source = 0.0f;
        for (const auto& [control, current] : values_)
            if (control.device == binding.control.device && control.code == binding.control.code
                && (binding.control.deviceId == 0 || binding.control.deviceId == control.deviceId))
                source = std::abs(current) > std::abs(source) ? current : source;
        if (std::abs(source) <= binding.deadzone) source = 0.0f;
        value += source * binding.scale;
    }
    return std::clamp(value, -1.0f, 1.0f);
}

void InputMapping::refreshEdges()
{
    for (auto& [action, state] : states_)
    {
        const auto wasDown = state.down;
        state.value = actionValue(action);
        state.down = std::abs(state.value) >= 0.5f;
        if (!wasDown && state.down) state.pressed = true;
        if (wasDown && !state.down) state.released = true;
    }
}

InputActionState InputMapping::state(const std::string& action) const noexcept
{
    const auto found = states_.find(action);
    return found == states_.end() ? InputActionState{} : found->second;
}

std::vector<std::string> InputMapping::actions() const
{
    std::vector<std::string> result;
    result.reserve(bindings_.size());
    for (const auto& [action, _] : bindings_) result.push_back(action);
    return result;
}
}

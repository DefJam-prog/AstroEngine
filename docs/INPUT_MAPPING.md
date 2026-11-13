# Input Mapping 04.2

`astro_runtime_input` не зависит от SDL или окна. Игровые системы связывают именованные actions с backend-neutral `InputControl` и читают `InputActionState`:

```cpp
using namespace astro::runtime;
InputMapping input;
input.bind("move_forward", {{InputDevice::KeyboardKey, 26}, 1.0f, 0.0f});
input.bind("move_forward", {{InputDevice::GamepadAxis, 1}, -1.0f, 0.18f});
input.bind("look_x", {{InputDevice::MouseDelta, 0}, 0.01f, 0.0f});

// В начале каждого input frame:
input.beginFrame();
// Затем backend adapter отправляет события/значения.
const auto movement = input.state("move_forward");
const auto look = input.state("look_x").value;
```

Bindings могут объединять несколько источников; `scale` задаёт знак/чувствительность, gamepad axis binding может иметь deadzone. Action state содержит объединённый `value` [-1,1], `down`, edge flags `pressed`/`released`. Mouse delta и wheel — transient значения, очищаемые `beginFrame`. Имена action: ASCII буквы, цифры, `_`, `.`, `-`, максимум 128 байт; не более 32 привязок на action.

Optional `astro_runtime_sdl_input` переводит SDL3 keyboard scancode, mouse button/motion/wheel и gamepad button/axis/removal события в карту. SDL keyboard input использует scancodes, binding может задавать конкретный gamepad ID или 0 для любого устройства. Приложение должно вызывать `beginFrame`, обработать SDL events и читать actions в согласованном порядке.

Проверены только синтетические SDL events в Windows/MSVC smoke-тесте. Не реализованы UI назначения клавиш, сохранение профиля ввода, hot-plug UI, вибрация, gamepad enumeration/ownership, Android/iOS/macOS adapters и физические устройства. SDL adapter — optional и не добавляет SDL-зависимость в headless `astro_runtime_input`.

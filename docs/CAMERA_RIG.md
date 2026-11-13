# Camera Rig 04.3

`astro_runtime_camera` предоставляет runtime API `astro::runtime::CameraRig`, который вычисляет pose камеры, но не рисует изображение. Пример:

```cpp
astro::runtime::CameraRig camera;
camera.setMode(astro::runtime::CameraMode::ThirdPerson);
auto settings = camera.settings();
settings.target = playerEntityId;
settings.followSharpness = 5.0; // 1/сек, 0 означает snap
settings.zoom = 1.25;
settings.minZoom = 0.5;
settings.maxZoom = 3.0;
if (!camera.configure(settings)) report("Invalid camera settings");
camera.snapToTarget(runtimeScene);
camera.update(runtimeScene, frameDeltaSeconds);
const auto pose = camera.pose();
```

Поддерживаются `Custom`, `FirstPerson`, `ThirdPerson`, `Isometric`, `TopDown`, `Platformer`, `Strategy`. `setMode` выбирает начальные world-space offset и Euler rotation; после этого настройки можно менять через `configure`. FirstPerson наследует Euler rotation target. Остальные режимы сохраняют назначенную Euler rotation и не вычисляют look-at. Custom использует явную pose через `setPose` и не следует за target.

`update` читает Transform целевой сущности, прибавляет follow offset и ограничивает позицию опциональными AABB bounds. Сглаживание экспоненциальное, задаётся sharpness в обратных секундах. `snapToTarget` сразу устанавливает вычисленную position/rotation. Zoom ограничивается диапазоном (по умолчанию 0.1–10). Ошибочные delta/settings, отсутствующий target и неверные bounds диагностируются без частичной публикации настроек.

Ограничения: берётся локальный Transform target, так как world-transform/hierarchy composition пока отсутствует; offset задаётся в world axes, ThirdPerson не наводится автоматически на цель. Нет projection/view matrices, FOV, renderer integration, Scene camera component, сериализации, editor UI или ручного визуального теста. Это проверяемая pose/follow логика, не готовый видимый camera system.

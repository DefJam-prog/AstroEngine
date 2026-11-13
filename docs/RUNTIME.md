# Runtime Session 04.1

`astro_runtime_session` обеспечивает ранний headless Play Mode без editor UI и без игрового renderer.

```cpp
astro::runtime::RuntimeSession runtime;
auto started = runtime.play(authoringScene,
    [](astro::scene::Scene& playScene, double fixedDelta) {
        // Обновление игровых систем только внутри playScene.
        return astro::core::Result<void>::success();
    });
if (!started) report(started.error().message);
auto frame = runtime.tick(realFrameDeltaSeconds);
runtime.pause();
auto oneTick = runtime.stepOnce(); // только пока Paused
runtime.resume();
runtime.stop();
```

`play` требует фиксированный callback и создаёт отдельную копию сцены. `tick` передаёт callback ровно фиксированное число шагов от `TimeService`; пауза подавляет вызовы. `stepOnce` выполняет ровно один fixed step и оставляет сессию на паузе. Ошибка callback переводит сессию в `Faulted`, сохраняя runtime-копию для диагностики до `stop`. `stop` освобождает копию; авторская сцена не изменяется сессией.

Переходы с неверного состояния возвращают `InvalidState`; неверный timestep или отсутствующий callback отклоняются. Исключение из callback преобразуется в `InternalError` и переводит сеанс в `Faulted`. Контракт не содержит оконного Play UI, input, physics, rendering, игровых модулей и realtime loop; поведение существует только в переданном callback.

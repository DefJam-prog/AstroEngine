# Runtime Interaction System 04.4

`astro_runtime_interaction` связывает target EntityId с runtime-only `InteractableBehavior` и публикует типизированные `InteractionEvent` через существующий `core::EventBus`:

```cpp
astro::runtime::InteractionSystem interactions;
astro::core::EventBus events;
auto subscription = events.subscribe<astro::runtime::InteractionEvent>(
    [](const auto& event) { log(event.sequence, event.kind); });

auto registered = interactions.registerInteractable(runtimeScene, doorId,
    {2.0, true, [](astro::scene::Scene& playScene, auto actor, auto target) {
        // Изменения применяются только к Scene, переданной runtime-вызовом.
        return playScene.setProperty(target, "name", std::string{"Opened door"});
    }});
auto result = interactions.requestInteraction(runtimeScene, events, playerId, doorId);
```

Поведению задаются activation range (0..1,000,000 world units), enabled-state и callback. Запрос сначала публикует `Requested`, затем проверяет actor/target, компонент, enabled-state, Transform и евклидову дистанцию. Успех вызывает callback и публикует `Activated`; отклонённый запрос или callback failure/exception публикует `Rejected` с диагностикой. Event sequence монотонный в пределах экземпляра InteractionSystem.

Bindings хранятся в памяти отдельного `InteractionSystem` и привязаны к одному `SceneId`; смешать одинаковые числовые EntityId разных сцен нельзя. Их нужно регистрировать для runtime-копии после Play. Callback получает mutable `Scene&`, поэтому используйте `RuntimeSession::runtimeScene()`, не authoring Scene. События синхронно dispatch через EventBus. Сбой observer на `Requested` прерывает запрос до callback; сбой observer на `Activated` происходит после callback и не откатывает его эффекты. Поведение поэтому не транзакционно.

Это кодовая/runtime binding система, не сохраняемый Scene component или GUI authoring workflow. Нет physics trigger/overlap, collision query, prompt UI, line-of-sight, input dispatch или network sync. Range использует локальные координаты Transform, пока world transforms не реализованы.

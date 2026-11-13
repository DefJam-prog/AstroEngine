# Command API — ранний C++ контракт

Подэтап 01.4 добавил локальный `CommandRegistry` для вызова движковых операций через typed payload. В 02.3 первая операция Scene Model (`entity.set_property` v1) подключена к этому registry и используется редакторским Inspector. Визуальная логика и AI-адаптеры пока не реализованы.

## Схема команды

`CommandSchema` содержит стабильный ID и положительную версию. ID длиной до 128 ASCII символов должен быть dot-separated: каждый сегмент начинается с `a`–`z`, затем допускаются `a`–`z`, `0`–`9` и `_`; пример `scene.create`.

Регистрация задаёт шаблонные типы payload и output, обязательную функцию валидации и executor:

```cpp
struct SetSpeed
{
    double value;
};

commands.registerCommand<SetSpeed, void>(
    {"vehicle.set_speed", 1},
    [](const SetSpeed& payload) -> astro::core::Result<void> {
        if (!std::isfinite(payload.value) || payload.value < 0.0)
            return astro::core::Result<void>::failure(
                {astro::core::ErrorCode::ValidationError, "speed must be finite and non-negative"});
        return astro::core::Result<void>::success();
    },
    [](astro::core::EngineContext&, const SetSpeed&) -> astro::core::Result<void> {
        // Выполнить операцию над соответствующей подсистемой.
        return astro::core::Result<void>::success();
    });
```

В реальной команде executor меняет только принадлежащую соответствующей подсистеме модель. Пример выше показывает форму API, но конкретной vehicle-подсистемы в проекте пока нет.

Payload проверяется точным C++ типом. Валидатор исполняет реальные проверки и возвращает `Result<void>`; он должен быть без побочных эффектов. Для `Output` поддерживаются copy-constructible типы или `void`.

## Проверка и выполнение

```cpp
const SetSpeed payload{12.0};
auto preview = commands.validate("vehicle.set_speed", 1, payload); // проверяет, executor не вызывается
auto result = commands.execute<SetSpeed, void>("vehicle.set_speed", 1, payload);
```

`validate` — validation-only/dry-run: не вызывает executor и не меняет состояние через сам CommandRegistry. Результат не резервирует предусловия и не гарантирует, что они останутся истинны к последующему `execute`; execute всегда валидирует заново.

`execute` возвращает `Result<Output>`; для команд без результата используйте `Output = void`. Неизвестный ID даёт `NotFound`, несовпадение версии — `VersionMismatch`, неверный C++ тип — `TypeMismatch`, отказ валидатора — его ошибка (обычно `ValidationError`), исключения callbacks преобразуются в `InternalError`. Если executor уже изменил состояние до возврата ошибки/исключения, автоматического rollback нет.

## Транзакционные команды и Undo/Redo

Для обратимых изменений регистрируйте команду через `registerTransactionalCommand`. Callback не применяет состояние напрямую: он добавляет шаги в транзакцию.

```cpp
commands.registerTransactionalCommand<SetSpeed, void>(
    {"vehicle.set_speed", 1}, validator,
    [](EngineContext&, const SetSpeed& payload, CommandTransaction& transaction) -> Result<void> {
        const double previous = readCurrentSpeed();
        return transaction.addStep("set speed",
            [value = payload.value] { return writeSpeed(value); },
            [previous] { return writeSpeed(previous); });
    });

auto applied = commands.execute<SetSpeed, void>("vehicle.set_speed", 1, {12.0});
if (applied) commands.undo();
commands.redo();
```

Пример показывает требуемую форму callback; `readCurrentSpeed`/`writeSpeed` обозначают API конкретной игровой подсистемы, которая пока не создана. Транзакция применяет добавленные шаги по порядку. При ошибке шага уже применённый префикс отменяется в обратном порядке. Undo идёт в обратном порядке, Redo — в прямом. История линейная: новая успешная транзакция после Undo удаляет redo-ветку.

Каждый шаг должен атомарно завершаться успехом/ошибкой или компенсировать свои частичные эффекты сам: менеджер может отменить только шаги, которые вернули успех. Если rollback/recovery тоже не удался, ошибки объединяются, история очищается, а фактическое состояние следует считать требующим проверки/восстановления.

Обычный `registerCommand` считается non-transactional. После успешного validator история очищается до executor, даже если executor затем вернул ошибку: это барьер на случай частичного необратимого эффекта. Такие команды нельзя отменить через этот history. Внешние операции, например отправка сетевых запросов/сохранение файлов, следует оформлять явно как необратимые; не маскируйте их фиктивным обратным callback.

Один ID регистрируется только один раз в данном registry. Одновременная регистрация нескольких версий одного ID, миграция payload и удалённый/JSON вызов пока не поддерживаются. Версия — контракт вызывающей стороны, а не версия файла проекта.

## Audit и ошибки

`auditTrail()` возвращает копию локальной последовательности validate/execute/undo/redo, включая ошибки, версию и порядковый номер. Payload не копируется в audit. Хранилище пока растёт до времени жизни registry; persistence, ограничения размера и конкурентный доступ не реализованы. Registry и callbacks не потокобезопасны.

Валидатор и executor могут использовать `EngineContext`. Регистрируйте только команды с явными проверками доступа на уровне будущего интеграционного слоя; безопасность AI и разрешения ещё не входят в этап 01.

## Границы и следующие шаги

В 01.5 добавлены in-memory транзакции и Undo/Redo. Scene Model-команды `entity.set_property` и `entity.set_sprite_renderer` транзакционные; нет внешнего/неизменяемого audit, `dry-run diff`, сохранения истории и гарантированного восстановления при сбое самого rollback. Подробности: [отчёт 01.4](reports/01.4.md), [01.5](reports/01.5.md), [02.3](reports/02.3.md), [03.3](reports/03.3.md).

## Команда Scene Model 02.3

Регистрируйте модель сцены через `astro::scene::registerSceneCommands(registry, scene)`. Она добавляет `entity.set_property` версии 1. Пример payload:

```cpp
astro::scene::SetScenePropertyPayload payload{
    entityId, "name", std::string{"Новый объект Ω"}};
auto preview = commands.validate("entity.set_property", 1, payload);
auto applied = commands.execute<astro::scene::SetScenePropertyPayload, void>(
    "entity.set_property", 1, payload);
```

`validate` проверяет схему и не меняет сцену; `execute` проверяет повторно и применяет изменение. Payload содержит `std::variant<std::string, EntityId>`, однако текущая схема разрешает изменять только `name`. Значение имени обязано быть валидным UTF-8, непустым (кроме пробелов), не содержать NUL и занимать не более 240 байт. `id` доступен только для чтения.

В 02.4 команда `entity.set_property` Name стала транзакционной: предыдущая строка восстанавливается через Undo, повторное применение — через Redo. В 03.3 добавлена `entity.set_sprite_renderer` v1 с payload `{entity, AssetId, visible}`; новая компонента удаляется Undo, Redo возвращает прежнее значение. Она проверяет entity и ненулевой AssetId, но наличие AssetId в проектном Registry отдельно диагностируется редактором. Это покрыто integration-тестами. Нет внешнего/неизменяемого audit, `dry-run diff`, сохранения истории и гарантированного восстановления при сбое rollback. Подробности: [отчёт 02.4](reports/02.4.md), [03.3](reports/03.3.md).

## Prefab-команды (03.4)

После `astro::prefabs::registerPrefabCommands(registry, library, scene)` доступны:

- `prefab.instantiate` v1 (`PrefabId`, optional parent EntityId) → новый root EntityId.
- `prefab.set_override` v1 (`instanceRoot`, source key, property kind, typed value).
- `prefab.update_instance` v1 (`instanceRoot`) — обновляет дерево по текущим файлам библиотеки.

Все три команды транзакционные. Undo/Redo сохраняют полные копии Scene, что просто и проверяемо, но потребляет память пропорционально размеру сцены. Валидация выполняет операцию на копии и не меняет live Scene. Пример API и сериализации: [отчёт 03.4](reports/03.4.md).

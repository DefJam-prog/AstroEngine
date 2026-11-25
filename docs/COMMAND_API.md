# Command API — ранний C++ контракт

Подэтап 01.4 добавил локальный `CommandRegistry` для вызова движковых операций через typed payload. В 02.3 первая операция Scene Model (`entity.set_property` v1) подключена к этому registry и используется редакторским Inspector. В 06.1 появилась отдельная pure dataflow Visual Logic prototype; её узлы пока не вызывают команды и не меняют Scene. AI-адаптеры пока не реализованы.

В 07.2be Hierarchy создает встроенный примитив командой `entity.create_primitive` v1 с payload `{name, parent?, primitive, visible}` и получает стабильный EntityId. Внешний typed-клиент и GUI проходят через один CommandRegistry. Undo удаляет созданную сущность; Redo восстанавливает её и стабильный PrimitiveRenderer ComponentId. `entity.set_primitive_renderer` v1 добавляет/меняет компонент на существующей сущности. `entity.delete_subtree` v1 удаляет сущность вместе с потомками; Undo восстанавливает снимок сцены и prefab-instance metadata, Redo повторяет удаление. Валидация команд проверяется на копии сцены, без изменения live state. Полные снимки в delete transaction требуют памяти пропорционально размеру сцены. Интеграционное покрытие проверяет dry-run, stable IDs, hierarchy, Undo/Redo; editor smoke отправляет четыре вида примитивов через BGFX Scene View. См. [отчёт 07.2be](reports/07.2be-primitive-transactions-render-smoke.md).

В 07.2bf Inspector применяет выбор формы и видимости существующего `PrimitiveRenderer` через ту же команду `entity.set_primitive_renderer` v1 с payload `{entity, primitive, visible}`. Оба поля отправляются одним транзакционным изменением; Undo/Redo восстанавливают предыдущую/новую комбинацию, сохраняя ComponentId. Размер и положение редактируются отдельно через Transform. Интеграционный тест проверяет переход Cube/visible → Sphere/hidden, отмену и повтор. [Отчёт 07.2bf](reports/07.2bf-primitive-inspector-authoring.md).

В 07.2bg назначение/сброс проектного материала примитиву идёт через `entity.set_primitive_material_override` v1 с payload `{entity, material}`; AssetId 0 очищает override. Команда транзакционная, dry-run проверяет наличие PrimitiveRenderer и зарезервированный sentinel. Inspector предлагает только материалы, зарегистрированные в открытом проекте, и разрешает материал до применения. Команда не проверяет registry для внешних клиентов: вызывающая сторона должна передавать project Material AssetId.

В 09.3i добавлена `entity.set_transform` v1: полная локальная трансформация существующей сущности задаётся typed payload `SetTransformPayload{entity, value: Transform{position, rotationDegrees, scale}}`. `CommandSchema` публикует десять скалярных полей: `entity` и девять `Number` с именами `position_x/y/z`, `rotation_degrees_x/y/z`, `scale_x/y/z`. Валидация вызывает существующий `Scene::setTransform` на копии Scene и принимает только конечные значения; новые диапазоны и запрет нулевого масштаба не вводятся. Исполнитель хранит before-value и Transform ComponentId; применение, Undo и Redo используют `Scene::setTransform` и проверяют, что ComponentId остался тем же. Команда меняет только локальный Transform, не вычисляет мировое преобразование и не подключает AI Change Review UI. Интеграционные проверки покрывают каталог схемы, dry-run, отсутствующую сущность, NaN, применение и Undo/Redo.

В 09.3p добавлена транзакционная `entity.set_transforms` v1. Typed payload — `SetEntityTransformsPayload{assignments: vector<EntityTransformAssignment>}`, где каждая запись содержит точный `EntityId` и полный local `Transform`. В каталоге поле `assignments` имеет отдельный тип `TransformAssignmentArray`; это не JSON/remote codec и его нельзя сериализовать через `NumberArray`. Валидация отклоняет пустой набор, число назначений больше числа сущностей Scene, повторный EntityId и неверные/отсутствующие Transform. Все назначения проверяются на копии Scene до изменения live state. No-op назначения пропускаются; изменённые значения применяются одной transactional history entry и сохраняют ComponentId. Apply/Undo повторно сверяют ожидаемые до/после значения и ID компонента. UI сохраняет невыбранные поля каждого Transform и использует эту команду, не вызывает `entity.set_transform` циклом.

В 09.3k добавлена `entity.reparent` v1 с typed payload `SetEntityParentPayload{entity, parent?}`; в каталоге оба значения — `UnsignedInteger`, а `parent = 0` обозначает корень. Dry-run проверяет копию сцены и существующие правила hierarchy/cycle. Транзакция сохраняет прежнюю связь и Transform ComponentId, меняет только parent link; Undo/Redo перепроверяют ожидаемого текущего родителя. Локальный Transform и связи поддерева сохраняются, мировая поза пересчитывается потребителями. AI Change Review не вызывает внутренний setter напрямую: оно повторно сверяет исходную и целевую цепочки, ComponentId/Transform и снимок затронутого поддерева, затем вызывает эту команду. См. [09.3k report](reports/09.3k-entity-reparent-review.md).

## Схема команды

`CommandSchema` содержит стабильный ID и положительную версию. ID длиной до 128 ASCII символов должен быть dot-separated: каждый сегмент начинается с `a`–`z`, затем допускаются `a`–`z`, `0`–`9` и `_`; пример `scene.create`.

Поле метаданных команды может иметь тип `Utf8String`, `UnsignedInteger`, `Boolean`, `AssetId`, `StringOrUnsignedInteger`, `Number`, `NumberArray`, `UnsignedIntegerArray` или `TransformAssignmentArray`. Последний — структурированный typed-only marker для списка пар EntityId/local Transform, а не универсальный кодек. `Number` описывает числовой параметр, в том числе `double`; конкретный payload всё равно проверяется статическим C++ типом и отдельным валидатором команды. Для новых типов схемы потребители, кроме C++ API, должны явно определить поддержку; автоматический внешний transport отсутствует.

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

Один ID регистрируется только один раз в данном registry. Одновременная регистрация нескольких версий одного ID, миграция payload и удалённый/JSON вызов пока не поддерживаются. В частности, `TransformAssignmentArray` доступен типизированным C++ потребителям; для внешнего клиента ещё нужен явный структурированный codec. Версия — контракт вызывающей стороны, а не версия файла проекта.

## Audit и ошибки

`auditTrail()` возвращает копию локальной последовательности validate/execute/undo/redo, включая ошибки, версию и порядковый номер. Payload не копируется в audit. Хранилище пока растёт до времени жизни registry; persistence, ограничения размера и конкурентный доступ не реализованы. Registry и callbacks не потокобезопасны.

Валидатор и executor могут использовать `EngineContext`. Регистрируйте только команды с явными проверками доступа на уровне будущего интеграционного слоя; безопасность AI и разрешения ещё не входят в этап 01.

## Границы и следующие шаги

В 01.5 добавлены in-memory транзакции и Undo/Redo. Scene Model-команды `entity.set_property` и `entity.set_sprite_renderer` транзакционные; нет внешнего/неизменяемого audit, `dry-run diff`, сохранения истории и гарантированного восстановления при сбое самого rollback. Подробности: [отчёт 01.4](reports/01.4.md), [01.5](reports/01.5.md), [02.3](reports/02.3.md), [03.3](reports/03.3.md).

## Локальный AI Editor API (09.1)

`astro::core::AIEditorApi` (`astro/core/AIEditorApi.hpp`) — библиотечный фасад текущего процесса над `CommandRegistry`. `version()` возвращает API 1.0; `commandCatalog()` отдаёт зарегистрированные схемы в стабильном лексикографическом порядке; typed `validate` и `execute` делегируют в тот же registry. `RegisteredCommandSchema` включает ID, версию, человекочитаемое описание, описание полей payload, тип результата и признак транзакционности. Каталог отражает только команды, зарегистрированные конкретной конфигурацией Editor.

```cpp
astro::core::AIEditorApi aiApi(commands);
for (const auto& command : aiApi.commandCatalog())
    std::cout << command.schema.id << " v" << command.schema.version << '\\n';
auto preview = aiApi.validate("entity.set_property", 1, payload);
auto applied = aiApi.execute<astro::scene::SetScenePropertyPayload, void>(
    "entity.set_property", 1, payload);
```

Пока фасад принимает C++ payload конкретного типа. Он не сериализует JSON, не принимает произвольные текстовые команды и не проверяет разрешения/подтверждение; in-process клиент считается доверенным. Каталог — описание реально зарегистрированных команд, а не обещание их универсальной исполнимости из внешнего адаптера. Новые операции должны добавить фактический command registration, метаданные и тесты вместе с соответствующей подсистемой.

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

## Scene material override 07.2d

`registerSceneCommands` also registers `entity.set_mesh_material_override` v1 with typed payload `{entity, material}`. It requires a MeshRenderer on the target. `material` is a project Material AssetId; zero clears the override so the source glTF material applies. The Scene command validates the identifier shape and participates in validation/execute/audit/Undo/Redo; project registration and source freshness are checked by `resolveMeshRenderer`, which has access to AssetRegistry. The 07.2e Inspector lists only registered material assets, resolves the selected material before command execution, and routes assignment/clear through this command. The command changes authoring data only and does not trigger a material draw path. See [07.2d report](reports/07.2d-scene-material-bindings.md) and [07.2e report](reports/07.2e-meshrenderer-material-inspector.md).

В 04.14 добавлены транзакционные Scene-команды `script_behavior.add` (`entity`, `script_asset`, `managed_type`; создаёт disabled component), `script_behavior.set_enabled` (`entity`, `component`, `enabled`) и `script_behavior.remove` (`entity`, `component`). Они доступны через тот же `CommandRegistry` и `AIEditorApi` catalog. Undo/Redo добавления/удаления восстанавливают ComponentId и порядок binding-ов. Scene-команды проверяют entity, стабильный AssetId и managed type, но не зависят от project Registry; Editor Inspector предлагает только bindings актуальной зарегистрированной сборки и повторно проверяет её перед Add/Enable. Runtime Play consent gate остаётся обязательным и fail-closed. См. [04.14 report](reports/04.14-scriptbehavior-editor-authoring.md).

## Prefab-команды (03.4)

После `astro::prefabs::registerPrefabCommands(registry, library, scene)` доступны:

- `prefab.instantiate` v1 (`PrefabId`, optional parent EntityId) → новый root EntityId.
- `prefab.set_override` v1 (`instanceRoot`, source key, property kind, typed value).
- `prefab.update_instance` v1 (`instanceRoot`) — обновляет дерево по текущим файлам библиотеки.

Все три команды транзакционные. Undo/Redo сохраняют полные копии Scene, что просто и проверяемо, но потребляет память пропорционально размеру сцены. Валидация выполняет операцию на копии и не меняет live Scene. Пример API и сериализации: [отчёт 03.4](reports/03.4.md).
# Directional light commands (07.2bw)

- `light3d.add_directional` adds an enabled linear-white light at intensity `1`.
- `light3d.set_directional` sets three linear RGB channels in `[0,1]`, an intensity multiplier in `[0,64]`, and `enabled`.
- `light3d.remove_directional` removes the component.

These are typed, transactional commands and support Undo/Redo with stable `ComponentId` restoration. Invalid/non-finite values fail validation before mutation. Direction is derived from the owning entity hierarchy and cannot be set independently.
# Групповое переподчинение Scene (09.3n)

```cpp
astro::scene::SetEntityParentsPayload payload{
    {entityA, entityB}, parentId}; // parentId == 0 означает корень
auto preview = commands.validate("entity.reparent_many", 1, payload);
auto applied = commands.execute<astro::scene::SetEntityParentsPayload, void>(
    "entity.reparent_many", 1, payload);
```

Команда принимает `entities` как `UnsignedIntegerArray`, поэтому 64-битные EntityId не теряют точность. Дубли удаляются, выбранные потомки выбранных предков исключаются из корневого набора. Пустой/неизвестный набор, отсутствующая цель и образование цикла отклоняются. Все реальные parent changes входят в одну transactional history entry; Undo/Redo применяются к группе целиком. Local Transform и Transform ComponentId сохраняются; world pose при смене родителя пересчитывается и может измениться. `entity.reparent` v1 для одиночной операции не изменён.

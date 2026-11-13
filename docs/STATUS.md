# Статус проекта

Обновлено: 2026-11-13.

| Область | Состояние |
| --- | --- |
| Продуктовые требования P01–P14 | Записаны в PRODUCT_REQUIREMENTS.md; реализация отсутствует |
| Предварительный стек | Записан в TECH_STACK.md; не утверждён окончательно |
| Решение о стеке | Предложение D001 в DECISIONS.md |
| Слои архитектуры и контракты | Требования раздела 04 записаны в ARCHITECTURE.md и D002; реализация отсутствует |
| Графический редактор и UX | Требования раздела 05 записаны в EDITOR_UX.md и D003; реализация отсутствует |
| Save System & Checkpoint Editor | 05.4–05.5: selective slots + runtime checkpoint/respawn; editor authoring и расширенные игровые поля отсутствуют |
| Игровые системы и расширения | Требования раздела 07 записаны в GAME_SYSTEMS.md и D005; реализация отсутствует, требуется проверенная основа |
| Шаблоны игр T01–T10 | Требования раздела 08 записаны в GAME_TEMPLATES.md и D006; шаблоны, конструктор и smoke-тесты отсутствуют |
| Codex / AI Integration и безопасность | Требования раздела 09 записаны в AI_INTEGRATION.md и D007; API, адаптер, sandbox и AI Game Tester не реализованы |
| Прототип 00.3 | Windows x64/MSVC/D3D11 spike собран; CTest 4/4; результат ограничен этой средой |
| Дорожная карта | Раздел 10 записан в ROADMAP.md и D008; этапы 00–10 запланированы |
| Структура репозитория | Добавлены engine/core, engine/scene, editor, tests/unit и tests/smoke; прочие подсистемы добавляются по мере реализации |
| Лицензия проекта и third_party | Лицензия не выбрана; политика зависимостей не согласована |
| Подэтап 00.1 | Минимальное приложение, CMake/CTest; чистая baseline сборка и 3 CTest проверены в рамках 00.3 |
| Подэтап 00.2 | Локальные проверки/PowerShell-тесты и отчёт реализованы; полная C++/format приёмка того подэтапа не подтверждена, clang-tidy пропущен |
| Подэтап 01.1 | EngineContext, Result/Error, Logger, ConfigurationService реализованы; unit-тесты добавлены |
| Подэтап 01.2 | IEngineModule и ModuleRegistry: зависимости, порядок lifecycle, rollback init и обратный shutdown; unit-тесты добавлены |
| Подэтап 01.3 | EventBus, TimeService/fixed timestep и ModuleRegistry::tick реализованы; unit-тесты добавлены |
| Подэтап 01.4 | Typed CommandRegistry, версия/type checks, validation, execute/result и audit реализованы; unit-тесты добавлены |
| Подэтап 01.5 | In-memory transactions, transactional commands, Undo/Redo, rollback/barrier semantics реализованы; unit-тесты добавлены |
| Исходники и API | Core API 01.1–01.5; Scene v4 читает v1–v3; Transform, SpriteRenderer и prefab instance metadata; `astro_prefabs` с nested prefab, source mapping, overrides, update API и транзакционными командами |
| Подэтап 05.1 | Runtime PhysicsWorld2D: static/dynamic bodies, XY AABB overlap, gravity, simple resolution, Enter/Stay/Exit events; кодовые привязки, не сериализуются |
| Подэтап 05.2 | GameUiModel для меню/HUD/паузы/настроек; optional SDL3/ImGui demo, не зависит от Editor target |
| Подэтап 05.3 | Optional match-three rules (`ASTRO_BUILD_PUZZLE_RUNTIME`), scalar animation tracks, SDL synthesized audio cues; нет visual puzzle board или audio asset playback |
| Подэтап 05.4 | Explicit Entity/property profiles, indexed slots, schema/game version checks, CRC32, atomic write and backup fallback |
| Подэтап 05.5 | Runtime XY checkpoint triggers, callback conditions, one-shot/repeatable use, typed events, opt-in selected-profile respawn |
| Сборка и тесты | Windows x64/MSVC Debug: baseline optional 25/25, headless 20/20; полный 05.5 regression — optional 26/26, headless 21/21 |
| Политика проверок и документации | Требования записаны в TESTING.md и D009; это не результаты выполненных тестов |
| Недостающие документы целевого комплекта | docs/FILE_FORMATS.md, docs/TEMPLATE_GUIDE.md |
| Совместимость и лицензии зависимостей | Не проверены |

Сборка проверена из Visual Studio 2022 Developer Command Prompt x64; инструменты доступны через среду VS, хотя в обычном PowerShell не все присутствуют в PATH.

00.1/00.2, 00.3, 01.1–01.5, 02.1–02.4, 03.1–03.5, 04.1–04.4 и 05.1–05.5: см. отчёты этапов. Следующий рекомендуемый подэтап — 05.6.

## Подэтап 05.1

`astro_runtime_physics2d` выполняет дискретные AABB столкновения XY для runtime Scene, интегрирует динамические тела с gravity и публикует события контакта. Ограничения: только static/dynamic, простая коррекция положения, нет CCD/вращения/масс/материалов/редакторского компонента; полный CTest 19/19. См. [PHYSICS_2D.md](PHYSICS_2D.md) и [отчёт](reports/05.1.md).

## Подэтап 05.2

`astro_runtime_game_ui` управляет переходами экранов, HUD-полями и схемой настроек; старт/пауза/возобновление/выход в меню синхронизированы с RuntimeSession. Optional ImGui adapter рисует экраны в SDL3 demo окне. HUD/settings не сериализуются; нет аудиосистемы, локализации и контроллерной навигации. Полный CTest 22/22. См. [GAME_UI.md](GAME_UI.md) и [отчёт](reports/05.2.md).

## Подэтап 05.3

`astro_runtime_puzzle` реализует deterministic seeded match-three rules, cascades, refill, score и level thresholds; `AnimationTrack` валидирует и интерполирует scalar keyframes. Optional SDL3 audio stream queues short synthesized cues and is used by the Game UI demo when available. Build/test profile: Windows x64/MSVC Debug, CTest 24/24. Нету serialized level/progress, visible puzzle board, scene animation binding, audio assets/mixer, spatialization или physical-speaker verification. См. [PUZZLE_AND_ANIMATION.md](PUZZLE_AND_ANIMATION.md) и [отчёт](reports/05.3.md).

## Подэтап 05.4

`astro_runtime_save` сохраняет только поля, перечисленные в `SaveProfile`, в слотах под заданным приложением user-data root. Поддержка v1: `entity.name`, scalar Transform fields и `sprite_renderer.visible`. Есть strict format/game/profile selection compatibility, CRC32 corruption detection, ограничение размера, atomic temp replace, `.bak` и backup fallback; отсутствующие объекты/компоненты учитываются в результате. Windows x64/MSVC optional CTest — 25/25 после реализации; headless профиль — 20/20. Миграций, автосохранения, tag/group profiles, checkpoint/respawn и GUI нет. См. [SAVE_SYSTEM.md](SAVE_SYSTEM.md) и [отчёт](reports/05.4.md).

## Подэтап 05.5

`astro_runtime_checkpoint` отслеживает вход actor в XY area зарегистрированных checkpoint entities, evaluates optional condition callbacks and publishes typed activation/rejection/respawn events. Supports one-shot/repeatable, position-only and SaveProfile-backed activation. Selected-profile respawn is opt-in, atomic with the runtime Scene copy, and finally sets checkpoint spawn position. Windows x64/MSVC full CTest: **26/26** optional and **21/21** headless. Definitions/runtime state are not scene-serialized; no death detection, editor UI, physics collision query, visual/sound, no-code conditions, health/inventory policies. См. [SAVE_SYSTEM.md](SAVE_SYSTEM.md) и [отчёт](reports/05.5.md).

## Подэтап 02.1
Добавлен optional astro_editor: окно, docking, меню и project.astro. Создание проекта отказывает для существующего каталога до каких-либо изменений; файл записывается во временный файл и публикуется без замены. Некорректное открытие не меняет активный проект. На завершении 02.1 CTest 6/6, включая smoke этих сценариев. См. [отчёт 02.1](reports/02.1.md).

## Подэтап 02.2
Реализованы in-memory Scene/Entity hierarchy, защита от циклов, создание root/child entity и удаление поддерева; редактор показывает Hierarchy и выбираемый structural Scene View. CTest 7/7. Scene persistence, component system и пространственный Transform не входят в подэтап; см. [отчёт](reports/02.2.md).

## Подэтап 02.3
Добавлены schema properties Name и read-only ID, Inspector и команда `entity.set_property` v1. Изменения Inspector проходят через CommandRegistry; интеграционные проверки покрывают validation и выполнение. CTest 8/8. Undo/Redo, компоненты и сериализация ещё не подключены; см. [отчёт](reports/02.3.md).

## Подэтап 02.4
Astro Scene v1 сохраняет ID, имя сцены, иерархию и имена сущностей. Редактор предоставляет Ctrl+S/O/Z/Y (также Ctrl+Shift+Z), диагностику и блокирует смену проекта/сцены при несохранённых изменениях. Undo/Redo покрывает транзакционное изменение Name; создание/удаление сущностей не включено в историю. CTest 9/9; подробности и ограничения — [отчёт 02.4](reports/02.4.md).

## Подэтап 03.2
`astro_assets` предоставляет project-local AssetRegistry. `.png`, `.jpg` и `.jpeg` импортируются только из `Assets/`, получают сохраняемый numeric AssetId и метаданные; encoded bytes зеркалируются в `Library/Assets` по SHA-256. Изменённый источник требует явного reimport, который сохраняет ID. Индекс хранится в `.astro/assets.index`. CTest 11/11; детали — [отчёт 03.2](reports/03.2.md).

## Подэтап 03.3
Добавлен SpriteRenderer с AssetId/visible и транзакционная `entity.set_sprite_renderer`; ссылки проходят scene persistence v3, загрузчик сохраняет поддержку v1/v2. Windows Editor импортирует PNG/JPEG, отображает WIC preview выбранного компонента и сообщает об отсутствующих источниках/AssetId. Компонент не является runtime/spatial renderer. CTest 11/11; детали — [отчёт 03.3](reports/03.3.md).

## Подэтап 03.4
Добавлен `astro_prefabs`: versioned `.astro-prefab` файлы, capture/instantiate, nested references с cycle/depth checks, source key mapping, Name/Transform/SpriteRenderer overrides, update с сохранением EntityId и локальных overrides. `prefab.instantiate`, `prefab.set_override`, `prefab.update_instance` транзакционные; Scene v4 сохраняет instance metadata/overrides, мигрирует v1–v3 при явном сохранении. Полный CTest 13/13; editor UI отсутствует. См. [отчёт 03.4](reports/03.4.md).

## Подэтап 03.1
Каждая сущность получает один обязательный `astro.Transform` версии 1 с устойчивым в пределах сцены Component ID, позицией, вращением Эйлера в градусах и масштабом. Astro Scene v2 записывает Transform и ID; файлы v1 мигрируют к значениям по умолчанию. CTest 9/9; ограничения — [отчёт 03.1](reports/03.1.md).

## Подэтап 03.5
`AssetDependencyGraph` сохраняет явные связи AssetId с cycle checks. Пакет строится по выбранному dependency closure, проверяет source/cache hash и использует target tags с детерминированным content hash и повторным использованием. Исходники остаются неизменны; payload — исходный PNG/JPEG, платформенная конвертация и Export/runtime loader отсутствуют. CTest 13/13. См. [отчёт 03.5](reports/03.5.md).

## Подэтап 04.1
`astro::runtime::RuntimeSession` добавлен как headless runtime contract. Scene копируется при Play, фиксированный callback вызывается только по шагам TimeService; Pause suppresses ticks, Step продвигает один шаг под паузой, Stop освобождает runtime-копию. Callback failures переводят сессию в Faulted. CTest 14/14; renderer/input/editor controls/game systems отсутствуют. См. [отчёт 04.1](reports/04.1.md).

## Подэтап 04.2
`astro_runtime_input` содержит независимые от SDL actions, digital/analog state, alternate bindings, scales/deadzone и edge states. Optional `astro_runtime_sdl_input` адаптирует SDL3 events keyboard/mouse/gamepad в общий API. Полный CTest — 16/16; physical devices и platform adapters кроме Windows не проверялись. UI для настройки bindings и сохранение профилей отсутствуют. См. [INPUT_MAPPING.md](INPUT_MAPPING.md) и [отчёт 04.2](reports/04.2.md).

## Подэтап 04.3
Добавлен `astro_runtime_camera`: Custom/FirstPerson/ThirdPerson/Isometric/TopDown/Platformer/Strategy pose presets, follow target, exponential smoothing, optional bounds и zoom range. Camera pose вычисляется по Transform entity, но Scene camera component, renderer, projection и look-at для follow camera не реализованы. CTest 17/17; визуальная проверка камеры невозможна без renderer. См. [CAMERA_RIG.md](CAMERA_RIG.md) и [отчёт 04.3](reports/04.3.md).

## Подэтап 04.4
`astro_runtime_interaction` добавляет runtime InteractableBehavior binding по EntityId, range/enabled validation, callback и typed sequenced Requested/Activated/Rejected events на core EventBus. Интеграционный тест подтверждает изоляцию runtime Scene и обработку ошибок. Компоненты не сериализуются, physics triggers и editor UI отсутствуют. CTest 18/18. См. [INTERACTION_SYSTEM.md](INTERACTION_SYSTEM.md) и [отчёт 04.4](reports/04.4.md).

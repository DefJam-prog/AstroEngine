# Этапы, подэтапы и критерии приёмки

Источник: раздел 10 требований пользователя. Подэтапы 00.1–00.3, 01.1–01.5, 02.1–02.4, 03.1–03.5, 04.1–04.4, 05.1–05.6 и 06.1–06.3 реализованы в ограниченном объёме по отчётам. Остальные этапы запланированы; наличие документов само по себе не означает прохождение приёмки.

00.1–00.3, 01.1–01.5, 02.1–02.4, 03.1–03.5, 04.1–04.4, 05.1–05.6 и 06.1–06.3: см. отчёты подэтапов.

Выполняется только явно назначенный пользователем подэтап. После него — отчёт и остановка до ручного подтверждения. Стек остаётся предварительным до успешной проверки 00.3; шаблоны создаются после реализации необходимых подсистем.

Нумерация этапов разработки 00–10 отличается от нумерации разделов требований 01–10. Упоминание «раздела 08» ниже относится к каталогу шаблонов GAME_TEMPLATES.md.

Каждый этап также подчиняется [TESTING.md](TESTING.md): unit/integration/smoke/regression-проверки, специальные проверки графики, производительности, сохранений и безопасности, а также документация, пример, тест и известные ограничения для каждой готовой функции.

## ЭТАП 00 — ОСНОВА РЕПОЗИТОРИЯ И ПРОВЕРКА СТЕКА

- **00.1** Создать структуру каталогов, README, ROADMAP, STATUS, ARCHITECTURE,
     DECISIONS, TESTING, .gitignore, CMakeLists.txt, минимальный app и тест.
     Приёмка: чистая конфигурация CMake, сборка и CTest; инструкции Windows.
- **00.2** CI/локальные скрипты проверки, форматирование, статический анализ
     по возможности, логирование и обработка ошибок.
     Приёмка: воспроизводимый отчёт о сборке и тестах.
- **00.3** Spike SDL3+bgfx+ImGui: окно, треугольник/примитив, ввод, docking.
     Приёмка: запускаемый прототип и документ о совместимости/лицензиях;
     альтернативы при сбое. Не объявлять стек утверждённым раньше.

## ЭТАП 01 — ЯДРО БЕЗ GUI

- **01.1** EngineContext, Result/Error, Logger, конфигурация.
- **01.2** ModuleRegistry, жизненный цикл, зависимости, ошибки.
- **01.3** EventBus, TimeService, fixed timestep.
- **01.4** CommandRegistry, schema, validation, execute/result.
- **01.5** Транзакции, Undo/Redo и тесты; документировать необратимые действия.

Приёмка: headless executable, тесты порядка запуска, событий, команд,
отмены, ошибочных входных данных и корректного завершения.

## ЭТАП 02 — МИНИМАЛЬНЫЙ РЕДАКТОР

- **02.1** Окно и docking layout; меню и проект.
- **02.2** Scene Model, Scene View, Hierarchy.
- **02.3** Inspector, schema properties, изменение через Command API.
- **02.4** Сохранение/открытие сцен, диагностика, Undo/Redo, hotkeys.

Приёмка: пользователь создаёт сцену и объект, меняет свойство,
сохраняет, перезапускает редактор, открывает сцену, отменяет действие.

## ЭТАП 03 — КОМПОНЕНТЫ, РЕСУРСЫ, ПРЕФАБЫ

- **03.1** Entity/Component IDs, Transform, базовая сериализация и миграции.
- **03.2** Asset Registry, импорт 2D-ресурсов, ссылки, кэш и переимпорт.
- **03.3** SpriteRenderer и отображение сцены; missing assets diagnostics.
- **03.4** Префабы, nested prefab, overrides, тест обновления экземпляров.
- **03.5** Пайплайн платформенных производных ресурсов и зависимости.

Приёмка: замена текстуры не ломает логику/ссылки; изменения префаба
применяются предсказуемо; неизвестные компоненты диагностируются.

## ЭТАП 04 — ИГРОВОЙ РЕЖИМ

- **04.1** Play/Stop/Pause/Step, изоляция состояния от редактора.
- **04.2** Input Mapping, клавиатура/мышь/геймпад.
- **04.3** Камеры и следование, переключение, параметры.
- **04.4** Runtime events и базовые интерактивные компоненты.

Приёмка: интерактивная сцена управляется и не портит исходную сцену.

## ЭТАП 05 — ПЕРВАЯ ЗАКОНЧЕННАЯ 2D-ИГРА

- **05.1** Минимальная 2D-физика/коллизии и события.
- **05.2** Game UI: меню, HUD, пауза, настройки.
- **05.3** Звук, анимации, игровая логика головоломки.
- **05.4** Save System v1: выбор сохраняемых полей, слоты, load/save,
     версионирование, защита от повреждённой записи.
- **05.5** Checkpoint v1: триггер и респаун, выбранные группы данных.
- **05.6** Windows runtime packaging; smoke-тест чистого запуска.

Приёмка: 2D-головоломка с несколькими уровнями, меню, звуком,
сохранением прогресса и самостоятельным Windows-приложением. Эта приёмка
пока не выполнена: 05.6 упаковывает проверенный Game UI prototype, тогда как
видимая доска/уровни и интеграция puzzle/save/checkpoint в приложение отсутствуют.

## ЭТАП 06 — NO-CODE И РЕДАКТОРЫ

Подэтап 06.1 реализован ограниченно: typed pure-dataflow prototype и базовая панель Visual Logic. Граф пока не сохраняется, не исполняет команды и не меняет Scene; отсутствуют flow-control и drag-wire authoring. Это не приёмка этапа 06.

Подэтап 06.2 реализован ограниченно: in-memory UI document authoring, две встроенные темы, Desktop/Tablet/Phone responsive preview и две валидируемые Scene bindings. Документ не сохраняется и не используется runtime Game UI; полная приёмка этапа 06 не достигнута.

Подэтап 06.3 реализован ограниченно: scalar timeline/keyframe authoring and preview playback, plus CameraRig presets/custom pose panel. Neither timeline values bind to Scene properties nor camera output is rendered; full Stage 06 acceptance remains open.

- **06.1** Visual Scripting, типизированные узлы и отладка.
- **06.2** UI Editor, темы, привязки, responsive layout.
- **06.3** Timeline/анимации, пресеты камер.
- **06.4** Save System Editor: профили, группы, визуальная расстановка
     checkpoint, ручное тестирование восстановления.
- **06.5** Библиотека первых 2D-шаблонов: Puzzle, Platformer, Top-Down.

Приёмка: новая механика собирается в GUI без написания C++.

## ЭТАП 07 — 3D И РЕДАКТОР МИРОВ

- **07.1** 3D meshes, glTF, 3D камеры, 3D физика.
- **07.2** Material Editor, PBR, освещение, тени.
- **07.3** Terrain Editor, растительность, дороги.
- **07.4** Water System, VFX/частицы, постобработка.
- **07.5** LOD, instancing, streaming, budgets и профили качества.

Приёмка: редактируемая 3D-сцена с измеренной производительностью.

## ЭТАП 08 — ПРОДВИНУТЫЕ МЕХАНИКИ И ШАБЛОНЫ

- **08.1** First Person Controller, Third Person Controller.
- **08.2** FPS Shooter: оружие, боеприпасы, враги, HUD, checkpoint.
- **08.3** Racing: транспорт, колёса, подвеска, трасса, таймер.
- **08.4** NPC, navigation, behavior, интерактивные объекты.
- **08.5** Шаблоны Isometric, 3D Adventure, Strategy и прочие из раздела 08.
- **08.6** Большие миры и расширенная транспортная система, включая прицепы.

Приёмка: каждый шаблон запускается, имеет инструкции и smoke-тесты.

## ЭТАП 09 — AI-FIRST РЕДАКТОР И ТЕСТЕР

- **09.1** Локальный AI Editor API, перечень команд и схемы.
- **09.2** Адаптер Codex, чтение состояния проекта и планирование изменений.
- **09.3** Preview/diff, permissions, checkpoints, rollback.
- **09.4** AI Game Tester: сценарии, логи, регрессии, отчёты.
- **09.5** AI-помощь в генерации шаблонов и оптимизации с валидацией.

Приёмка: Codex по текстовому запросу меняет сцену через API,
показывает изменения, пользователь принимает/отклоняет, тесты проходят.

## ЭТАП 10 — ПОРТИРОВАНИЕ И ВЫПУСК

- **10.1** macOS: проверка сборки, ввод, ресурсы, упаковка.
- **10.2** Android: ввод/касания, жизненный цикл, производительность, экспорт.
- **10.3** iOS: toolchain, подпись/сборка на совместимом Mac, ввод, упаковка.
- **10.4** Документация, примеры, миграции, релизные тесты.

Приёмка: подтверждённые сборки на реальных целевых toolchains;
Windows не должен ломаться. Не обещать iOS-сборку на Windows без Mac.

02.1–03.5, 04.1–04.4 и 05.1–05.6 реализованы в пределах ограничений [отчётов](reports/). Следующий шаг не нумерован в текущем плане: требуется отдельно определить работу для недостающей визуальной puzzle/game integration и полной приёмки этапа 05.

## Подэтап 03.5 — граф ресурсов и детерминированный package

Добавлены явные зависимости AssetId и пакетирование достижимых образов с target tags, SHA-256 cache и атомарной публикацией. Сцены/prefab не сканируются, платформенные форматы не создаются.

## Подэтап 04.3 — камеры и следование

Добавлен runtime CameraRig API для camera pose, пресетов и следования с параметрами. Рендеринг, Scene camera component и визуальное представление отсутствуют.

## Runtime Interaction and Events (04.4)

Реализованы programmatic runtime behavior bindings и typed interaction events. Данные поведения остаются вне Scene file.

## Подэтап 05.1 — минимальная 2D-физика и collision events

Добавлены runtime body/collider bindings, gravity, fixed-step XY AABB collision detection, базовая positional correction и sequenced Enter/Stay/Exit events. Данные остаются вне Scene serialization; ограничения — [PHYSICS_2D.md](PHYSICS_2D.md) и [отчёт](reports/05.1.md).

## Подэтап 05.2 — Game UI

Добавлена backend-neutral GameUiModel для Main Menu, Gameplay/HUD, Pause и Settings с координацией RuntimeSession. Optional SDL3/ImGui adapter и `astro_game_ui_demo` дают видимые экраны; настройки остаются in-memory. Ограничения и команды: [GAME_UI.md](GAME_UI.md), [отчёт](reports/05.2.md).

## Подэтап 05.3 — звук, анимации и игровая логика головоломки

Добавлены backend-neutral правила match-three и детерминированное sampling scalar animation tracks. Optional SDL3 adapter queues synthesized UI/gameplay cues; реальный выход проверяется через dummy driver. Нет visual puzzle board, asset audio playback, смешивания дорожек или editor authoring. Описание и ограничения: [PUZZLE_AND_ANIMATION.md](PUZZLE_AND_ANIMATION.md), [отчёт](reports/05.3.md).

## Подэтап 05.4 — Save System v1

Добавлен `astro_runtime_save`: ручные numbered slots, явно перечисляемые profile fields, game/profile/schema metadata, CRC32, ограничение размера, временная запись, атомарная замена, `.bak` и fallback при повреждении основного слота. В v1 доступны Name, scalar Transform и SpriteRenderer visibility; нет автоматического сохранения, schema migrations, tag/group rules, checkpoint/respawn, editor UI или платформенного выбора user-data root. См. [SAVE_SYSTEM.md](SAVE_SYSTEM.md), [FILE_FORMATS.md](FILE_FORMATS.md), [отчёт](reports/05.4.md).

## Подэтап 05.5 — Checkpoint v1

Добавлен runtime-only `CheckpointSystem` с XY area entry, условиями C++ callback, typed Activated/Rejected/Respawned events, one-shot/repeatable поведением и явным respawn. `RespawnOnly` не пишет сохранения; `SaveSelectedProfile` требует зарегистрированный profile и слот из `SaveStore`. Restore выбранного профиля отключён по умолчанию и после load выставляет respawn position. Нет scene/editor authoring, world-space/physics triggers, death detection, sound/visuals, no-code conditions или health/inventory rules. Подробности: [SAVE_SYSTEM.md](SAVE_SYSTEM.md), [отчёт](reports/05.5.md).

## Подэтап 05.6 — Windows runtime package

Optional CPack target creates a ZIP of the current SDL3/ImGui Game UI demo with third-party license texts. The smoke extracts the archive to a fresh unique directory and starts the executable there. This verifies packaging of the prototype only: the visible multi-level puzzle and connected progress persistence from Stage 05 acceptance are still missing. Results and commands: [report 05.6](reports/05.6.md), [BUILD_GUIDE](BUILD_GUIDE.md#упаковка-windows-runtime-этап-056).

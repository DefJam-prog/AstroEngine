# AstroEngine

AstroEngine is a universal modular 2D/3D game engine and editor in development. Its intended differentiator is AI-assisted game creation through the same typed project operations used by the GUI, with reviewable changes, validation and tests. Puzzle is the current vertical-slice example, not the product's genre limit. Unity/Godot/Unreal/CryEngine are long-term breadth references; current implementation status is recorded below and in [PRODUCT_REQUIREMENTS.md](docs/PRODUCT_REQUIREMENTS.md).

The optional 07.1 3D prototypes add bounded glTF static geometry import and Scene references, plus an Editor-only BGFX preview in Scene View. In 07.1g, right-drag orbits, middle-drag pans, the wheel zooms, and **Frame All** fits visible mesh bounds after hierarchy transforms. The 07.1h smoke checks two visible meshes, an unresolved asset diagnostic, their combined framing, and coarse mesh pixel coverage inside a cropped Scene View screenshot. The 07.1i `astro_editor.exe --benchmark-3d` profile measures mesh CPU preparation/submission and full Editor frame timings on deterministic scenes with 1/100/500/1000 meshes; on the current Windows D3D11 host, the 1000-mesh Scene View pass used 1.42–1.60 ms CPU p50 and the complete editor frame remained about 16.65–16.68 ms p50 with VSync. These are machine-specific observations, not performance budgets. Game View/runtime 3D, materials, lighting, picking, and non-Windows validation remain unsupported. See [3D rendering and import guide](docs/3D_RENDERING.md), [07.1i report](docs/reports/07.1i-editor-3d-frame-profile.md), [07.1h report](docs/reports/07.1h-editor-scene-visual-smoke.md), and [test results](docs/TESTING.md#071i-editor-3d-frame-profile).

Начало разработки модульного 2D/3D-движка на C++20. Headless-приложение выводит информацию о запуске. В `astro_core` есть базовые сервисы, ModuleRegistry, typed EventBus, TimeService/fixed timestep, typed CommandRegistry и in-memory транзакции/Undo/Redo. Optional Windows editor показывает Hierarchy, Inspector с Name и SpriteRenderer, сохраняет Astro Scene v5 и показывает WIC image preview. Scene v5 хранит ScriptBehavior-ссылки; Asset index v2 объединяет IDs ресурсов и разрешает зарегистрированный Script AssetId в source/type/assembly/runtimeconfig. При включённом optional C# host Editor после односессионного согласия запускает lifecycle enabled ScriptBehavior в изолированном Play. Подэтап 04.15 добавил базовую C# Code workspace для файлов проекта: просмотр `Scripts/**/*.cs`, создание/открытие/сохранение UTF-8, подтверждение при переключении грязного документа и запуск существующей сборки с диагностикой/переходом к строке. Редактор не имеет подсветки синтаксиса, LSP или отладчика; C# build доступен только в конфигурации с локальным SDK/host support. Core API поддерживает nested prefabs, overrides и обновление экземпляров; UI управления префабами ещё нет. Scene/Game View отображают иерархические SpriteRenderer; Game View поддерживает изолированный Play и optional Puzzle sample с ручными SaveStore-слотами. Это не полноценные игровые шаблоны. WIC loader пока ограничен Windows.

Для editor integrations доступен начальный in-process C++ `AIEditorApi`: каталог реально зарегистрированных команд, typed validation и вызов через общий CommandRegistry. Это пока не Codex adapter, JSON/network API или security boundary; детали и ограничения в [COMMAND_API.md](docs/COMMAND_API.md) и [AI_INTEGRATION.md](docs/AI_INTEGRATION.md).

В продуктовое ТЗ внесено P15: отдельная встроенная Code workspace для ручного редактирования игровых скриптов на C#. В 04.5–04.6 проверен экспериментальный Windows x64 C++ host через .NET `hostfxr` и versioned ABI v1. В 04.7 Scene v5 сохраняет ScriptBehavior; 04.8–04.10 связывают его с project build; 04.12 добавил одноразовое Editor-согласие; 04.13 подключил lifecycle в RuntimeSession. Подэтап 04.15 добавил базовый текстовый workflow для C# файлов. Sandbox, hot reload, LSP/отладчик и cross-platform host пока отсутствуют. Host остаётся опциональным (`ASTRO_BUILD_CSHARP_RUNTIME=OFF` по умолчанию). Подробности: [C# scripting API](docs/CSHARP_SCRIPTING_API.md) и [отчёт 04.15](docs/reports/04.15-csharp-code-workspace.md).

Windows Editor позволяет переключать интерфейс English/Russian через **Edit → Settings… → Language → Русский → Apply**; выбор сохраняется в каталоге пользователя SDL. SDL Game UI demo предоставляет отдельный переключатель языка на экране Settings и сохраняет его там же, вне проекта и игровых данных. Границы перевода и поведение системных диагностик описаны в [руководстве по языку](docs/LOCALIZATION.md).

В 00.2 добавлены локальные проверки с JSON-отчётом, настройка форматирования и опциональный статический анализ. Фактические результаты и ограничения: [отчёт 00.2](docs/reports/00.2.md). Подэтапы ядра 01.1–01.5 покрыты в [отчётах](docs/reports/).

## Сборка на Windows 11

Нужны CMake 3.20+, Ninja и настроенный C++20-компилятор с Windows SDK. Для MSVC используйте Developer PowerShell/Command Prompt x64. Windows x64 Debug-сборка и CTest подтверждены в Visual Studio 2022 Build Tools; актуальные результаты этапов указаны в `docs/reports/`. Эти инструменты не устанавливаются проектом.

```powershell
Set-Location 'C:\Users\zheka\Desktop\gpt\AstroEngine'
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug
& '.\build\windows-debug\engine\runtime\astro_headless.exe'
```

Каждую команду выполнять только после успешного завершения предыдущей. Подробности и чистая сборка: [BUILD_GUIDE](docs/BUILD_GUIDE.md).

Ожидаемый вывод headless приложения:

```text
[info] AstroEngine
[info] Mode: headless
[info] Language: C++20
[info] Startup complete
```

## Структура и ограничения

Для полного локального отчёта: `pwsh -NoProfile -File scripts/verify.ps1`. Нужен PowerShell 7+. Код 0 означает прохождение обязательных проверок, 1 — ошибку, 2 — неполную проверку из-за недоступных инструментов. Отчёты и отдельные логи находятся в новом каталоге `build/verification/<UTC-время>-<ID>/` каждого запуска. Скрипт не устанавливает инструменты и не отправляет данные.

Подэтап 06.50 сделал архивирование malformed campaign-transfer journal устойчивее: запись сначала завершается и проверяется во временном файле, затем публикуется атомарно без замены существующего архива. Исходный журнал остаётся для ручного разбора. При аварийном завершении временный остаток не удаляется автоматически; подробности и ограничения — в [SAVE_SYSTEM](docs/SAVE_SYSTEM.md#archive-a-readable-malformed-journal-0644-behavior-revised-in-0645) и [отчёте 06.50](docs/reports/06.50-staged-journal-archive.md).

Подэтап 06.51 проверяет остановку дочернего процесса в середине staging-записи, после проверки до публикации и сразу после публикации. Повторное архивирование сохраняет исходный журнал, slot-файл и прежние staging-остатки. Проверяется процессный сбой, а не потеря питания. См. [отчёт 06.51](docs/reports/06.51-archive-interruption-tests.md).

Оценка 06.52 установила, что чужой файл на итоговом пути архива нельзя распознать по fingerprint или совпадающему префиксу. В 06.53 Editor получил read-only диагностику: показывает ожидаемый путь, тип объекта, размер и сравнение ограниченного содержимого; ссылки не разыменовываются. Конфликт остаётся нетронутым, автоматического удаления/перемещения/замены нет. Подробности и ограничения: [отчёт 06.53](docs/reports/06.53-archive-conflict-diagnostics.md), [D116](docs/DECISIONS.md#d116--read-only-diagnostics-for-archive-conflicts-0653).

В 06.54 оценён будущий способ сохранить неизвестный обычный файл конфликта отдельной копией. Это пока только политика: продукт не создаёт такие копии. Предложение ограничено явным подтверждением, no-follow чтением обычных файлов не больше 4096 байт, новой соседней целью без замены, сохранением оригинала и ручной проверкой. Само копирование не доказывает происхождение файла и не разблокирует исходный путь. См. [отчёт 06.54](docs/reports/06.54-conflict-copy-assessment.md) и [D117](docs/DECISIONS.md#d117--bounded-explicit-copy-of-unknown-archive-occupants-0654-assessment).

В 06.55 реализована отдельная подтверждаемая копия ограниченного файла-конфликта: диалог показывает исходный и целевой путь, размер и предупреждает о неизвестном авторстве. API удерживает no-follow источник, проверяет точные байты и публикует соседнюю копию без замены; исходный конфликт, журнал и slot сохраняются. Копия остаётся непроверенной и не снимает конфликт. Проверены коллизии, устаревший источник, небезопасные типы и завершение процесса до/после публикации. См. [отчёт 06.55](docs/reports/06.55-conflict-copy.md), [D118](docs/DECISIONS.md#d118--explicit-bounded-copy-aside-for-archive-conflicts-0655).

В 06.56 проверен тот же copy-aside путь в Unicode-каталоге `Проверка копии Ω` на Windows и POSIX; байты копии, исходника, журнала и слота совпали с ожиданиями. Подтверждающий диалог вручную осмотрен в изолированном Editor smoke и отменён без копирования. Для повторного визуального просмотра есть тестовый запуск `astro_editor.exe --smoke-inspect-copy-dialog`; он сам использует временную фикстуру и ждёт ручной отмены. См. [отчёт 06.56](docs/reports/06.56-unicode-copy-and-dialog-review.md).

Приложение принимает `--help`; неизвестные или лишние аргументы дают код 2 и сообщение `[error]` в stderr. Обычный запуск возвращает 0; сбой вывода/исключение — 1. Текст stdout получил префикс `[info]`; внешним потребителям точного вывода нужно учитывать этот префикс. Это ранний CLI, не стабильный API движка.

- `engine/runtime/` — минимальная консольная точка входа, не готовый игровой runtime.
- `tests/smoke/` — проверка запуска процесса, кода возврата и вывода.
- `docs/` — требования, архитектура, дорожная карта и фактический статус.

Основная headless-сборка не требует внешних библиотек. Optional Windows editor, графический spike и Game UI demo используют локальные SDL3/bgfx/ImGui из build-кэша. Runtime имеет только минимальную 2D AABB физику; общего gameplay renderer, AI API и платформ кроме Windows пока нет. Лицензия проекта не выбрана.

См. [архитектуру](docs/ARCHITECTURE.md), [план](docs/ROADMAP.md), [статус](docs/STATUS.md), [решения](docs/DECISIONS.md), [проверки](docs/TESTING.md) и [отчёт 00.1](docs/reports/00.1.md).

Редактор 02.1: [запуск и ограничения](docs/reports/02.1.md). EXE: build/stack-spike/editor/astro_editor.exe.
Редактор 02.2: [модель сцены, Hierarchy и Scene View](docs/reports/02.2.md). Панели позволяют создавать сцену, root/child сущности, выбирать и удалять поддеревья. Scene View показывает структуру, не пространственные объекты.

Подэтап 03.3: [SpriteRenderer и image preview](docs/reports/03.3.md). AssetId сохраняется в сцене, компонент редактируется транзакционной командой; это preview выбранного компонента, не пространственный/runtime renderer.

Подэтап 03.4: [Prefab API](docs/reports/03.4.md). Есть versioned файлы, вложенные ссылки, overrides и update; панели editor для authoring пока отсутствуют.
Редактор 02.3: [Inspector и schema properties](docs/reports/02.3.md). Сейчас схема включает обязательное имя сущности и read-only ID; команда изменения проверяет UTF-8 и ограничение 240 байт.

Подэтап 06.1: optional Editor содержит Visual Logic dataflow prototype с типизированными числами/логическими/текстовыми узлами и пошаговой трассой. Соединения выбираются из типизированного меню; графы пока не сохраняются и не меняют Scene. См. [документацию Visual Logic](docs/VISUAL_LOGIC.md).

Подэтап 06.2: панель `View → UI Editor` создаёт Text/Button/Panel элементы, редактирует тёмную/светлую тему и responsive anchors/offsets, показывает Desktop/Tablet/Phone preview и поддерживает привязки `Entity.Name` и `SpriteRenderer.Visible`. UI-документы пока живут только в памяти редактора и не отображаются в игровой сборке. См. [руководство UI Editor](docs/UI_EDITOR.md).

Подэтап 06.3: панели `View → Timeline` и `View → Camera Presets` редактируют/проигрывают один scalar animation track и вычисляют camera pose через существующие пресеты CameraRig. Это editor preview: animation не привязана к Scene properties, camera pose не рендерится, документы не сохраняются. См. [Timeline](docs/TIMELINE_EDITOR.md) и [Camera Rig](docs/CAMERA_RIG.md).

Подэтап 06.4: окно `View → Save System` позволяет создавать пустые профили, явно выбирать сохраняемые свойства, составлять группы сущностей, подключать правила группы к профилю, настраивать checkpoint и проверять активацию/respawn на копии сцены с временным хранилищем. Данные authoring пока in-memory; ограничения: [Save System Editor](docs/SAVE_SYSTEM_EDITOR.md), результаты: [отчёт 06.4](docs/reports/06.4.md).

Подготовительная основа для 06.5: optional `astro_runtime_render2d`, SDL3 адаптер и отдельный Play preview с fixed-step RuntimeSession, цветной геометрией и root `SpriteRenderer` из AssetRegistry. Детали и ограничения: [RENDERER_2D.md](docs/RENDERER_2D.md).

Для T01 Puzzle есть детерминированный `PuzzleSnapshot`, optional `astro_runtime_puzzle_save` адаптер и Game View preview с фиксированным шагом и ручными слотами только для выбранных свойств. Авторские уровни хранятся в `Puzzle/Levels/<ID>.astro-puzzle-level`; кампании — в `Puzzle/Campaigns/<ID>.astro-puzzle-campaign`. Окно **View → Puzzle Campaign Browser** показывает ссылки и позволяет редактировать порядок, START и переходы; подтверждённое сохранение проходит строгую проверку. Переименование уровня блокируется, если на него ссылается кампания или ссылки не удалось проверить: сначала обновите и сохраните кампанию. Editor Game View запускает кампанию по ID и явно продолжает после достижения цели; при `END` финальная доска остаётся видимой. Теперь в Game View можно вручную сохранять и загружать кампанийный прогресс в слотах 0–9; профиль проверяет ID кампании и уровня, END и Puzzle snapshot. Файлы лежат в SDL user data отдельно от проекта. При смене ID кампании выбирается отдельный namespace без переноса старых слотов. Перенос или копия проекта с `.astro/puzzle-save.settings` сохраняет ту же папку прогресса, поэтому копии делят слоты, пока не задан другой каталог в настройках. Автоматической миграции нет. Это не готовый T01 шаблон. См. [Puzzle Level Authoring](docs/PUZZLE_LEVELS.md), [Puzzle Campaign Format](docs/PUZZLE_CAMPAIGNS.md), [Save System](docs/SAVE_SYSTEM.md) и [отчёты 06.31–06.33](docs/reports/06.31-editor-campaign-progress.md).

### Сцены редактора (02.4)

В редакторе доступны сохранение/открытие сцены (Ctrl+S, Ctrl+O), Undo/Redo (Ctrl+Z, Ctrl+Y или Ctrl+Shift+Z) и панель Diagnostics. Astro Scene v5 сохраняет ID/имена сцены и сущностей, иерархию, Transform, SpriteRenderer AssetId, prefab instance metadata и ScriptBehavior binding records. Загрузчик читает v1–v4; старые файлы переписываются в v5 только при явном сохранении. Asset index v2 добавляет глобальные script AssetIds и разрешение существующей сборки, мигрируя v1 только при явной операции записи. При несохранённых правках переключение проекта/сцены блокируется. Форматы и миграции описаны в [FILE_FORMATS.md](docs/FILE_FORMATS.md).

Каждая новая сущность получает обязательный Transform с позицией, углами Эйлера в градусах и масштабом. Доступен C++ API `scene.transform(entityId)` и `scene.setTransform(entityId, value)`; значения должны быть конечными числами. Пространственный рендеринг и вычисление мировых матриц пока отсутствуют.

### Импорт изображений в проекте (03.2)

`astro_assets` предоставляет `AssetRegistry::open(projectRoot)`, `importImage(relativePath)` и `reimport(AssetId)`. Источники PNG/JPEG должны находиться внутри `Assets/`; их исходные байты не меняются. Индекс стабильных ID находится в `.astro/assets.index`, проверенные исходники зеркалируются по SHA-256 в генерируемый `Library/Assets` (эта папка добавлена в `.gitignore`). Пример и ограничения: [ASSET_PIPELINE.md](docs/ASSET_PIPELINE.md).

Подэтап 03.5: реализованы явный граф AssetId и target-tagged deterministic package из выбранных ресурсов и зависимостей. Формат сохраняет исходные PNG/JPEG bytes; см. [pipeline](docs/ASSET_PIPELINE.md) и [отчёт](docs/reports/03.5.md). Это не полноценный платформенный экспорт.

Подэтап 04.1: появился headless `RuntimeSession` с изолированной Scene-копией и Play/Pause/Resume/Step/Stop. Проверки и границы: [runtime API](docs/RUNTIME.md), [отчёт 04.1](docs/reports/04.1.md). Редакторские кнопки, рендеринг и игровые системы пока не подключены.

Подэтап 04.2: добавлены backend-neutral actions и optional SDL3 adapter для keyboard/mouse/gamepad событий. Тесты и ограничения: [Input Mapping API](docs/INPUT_MAPPING.md), [отчёт 04.2](docs/reports/04.2.md). Профили ввода, editor UI назначения клавиш и другие платформенные adapters не входят в эту реализацию.

Подэтап 04.2: backend-neutral Input Mapping и optional SDL3 adapter для клавиатуры, мыши и геймпада; [описание API](docs/INPUT_MAPPING.md), [отчёт](docs/reports/04.2.md).

Подэтап 04.3: добавлен headless `CameraRig` для camera pose, preset modes, следования за target Transform, smoothing, bounds и zoom. Он не визуализирует изображение и не добавляет camera component в формат Scene. См. [описание](docs/CAMERA_RIG.md) и [отчёт 04.3](docs/reports/04.3.md).

Подэтап 04.3: runtime CameraRig для pose/follow, пресетов, smoothing, границ и zoom. Рендеринг камеры и Scene camera component пока отсутствуют; [API](docs/CAMERA_RIG.md), [отчёт](docs/reports/04.3.md).

Подэтап 04.4: runtime InteractionSystem связывает entity с code-defined InteractableBehavior, проверяет дальность/активацию и публикует события Requested/Activated/Rejected через typed EventBus. Система может менять только переданную runtime Scene; это не сохранённый no-code component. [API и ограничения](docs/INTERACTION_SYSTEM.md), [отчёт](docs/reports/04.4.md).

Подэтап 05.1: `astro_runtime_physics2d` предоставляет fixed-step runtime API для динамических тел, axis-aligned box overlaps, простого solid resolution и collision Enter/Stay/Exit events. Данные пока не сериализуются и не настраиваются через GUI; ограничения и пример: [PHYSICS_2D](docs/PHYSICS_2D.md), [отчёт 05.1](docs/reports/05.1.md).

Подэтап 05.2: `GameUiModel` управляет меню, запуском/паузой `RuntimeSession`, HUD и настройками. 05.3 добавляет match-three rules, scalar animation sampling и optional SDL3 cues; 05.4 — selective save slots; 05.5 — runtime checkpoint triggers и respawn поверх явно выбранного SaveProfile. В 05.7 в Game UI demo появилась Puzzle-доска, 05.8 подключил авторские campaign-файлы, а 05.9 добавил ручное сохранение выбранного прогресса кампании в standalone runtime. См. [Save System и Checkpoints](docs/SAVE_SYSTEM.md), [Puzzle campaigns](docs/PUZZLE_CAMPAIGNS.md) и [отчёт 05.9](docs/reports/05.9-campaign-progress-save.md).

Подэтап 05.6 упаковывает Game UI runtime demo в Windows ZIP с README и лицензиями SDL3/ImGui. В 05.10 к архиву добавлены пример кампании в `game-data/puzzle-campaign` и launcher `launch_puzzle_campaign.cmd`, запускающий пример относительно расположения самого архива. Demo вручную сохраняет выбранный прогресс в пользовательском каталоге. ZIP не содержит редактора, build tools или произвольный проект; полноценный экспорт и завершённая 2D-игра остаются незавершёнными. Чистая распаковка проверяет меню и bundled campaign save/restore; launcher также проверен вручную из другого рабочего каталога. Инструкции: [Windows runtime packaging](docs/BUILD_GUIDE.md#упаковка-windows-runtime-этап-056), [отчёт 05.10](docs/reports/05.10-self-contained-campaign-package.md).

2D SpriteRenderer показывается в Editor **Scene View** и **Game View**; Scene View также сохраняет вкладку карты иерархии. Обычный Play/Pause/Step/Stop управляет изолированной Scene-копией. Дополнительно `Play Puzzle` запускает optional Puzzle runtime с кликабельной доской и fixed-step ходами. Это не изменяет редактируемую сцену. Подробности и ограничения: [2D renderer и редакторские preview](docs/RENDERER_2D.md), [результаты 06.15](docs/reports/06.15-puzzle-game-view.md).

Для сравнения Scene/Game View preview запустите `build/stack-spike/editor/astro_editor.exe --benchmark`. Editor рисует окна напрямую в bgfx framebuffer; результаты сравнения с предыдущим CPU preview приведены в [отчёте 06.10](docs/reports/06.10-bgfx-framebuffer-renderer.md), исходный baseline — [отчёт 06.9](docs/reports/06.9-renderer-performance.md).

## 04.12–04.13 — согласие и C# ScriptBehavior в Play

Перед Play Editor показывает проект/сцену, точный список включённых `ScriptBehavior`, source/type и fingerprint с SHA-256 выходов свежей сборки. Подтверждение действует на одну сессию и отзывается при Stop, новом Play, смене проекта/сцены или ошибке. В 04.13 RuntimeSession запускает lifecycle callbacks на изолированной копии сцены; ошибка загрузки/`OnCreate` отменяет Play и выполняет rollback, Stop вызывает `OnDestroy` в обратном порядке. Скрипты остаются доверенным in-process кодом без sandbox. Подробности: [C# scripting API](docs/CSHARP_SCRIPTING_API.md), [отчёт 04.12](docs/reports/04.12-csharp-editor-approval.md) и [отчёт 04.13](docs/reports/04.13-csharp-runtime-lifecycle.md).

## 04.14 — авторинг ScriptBehavior

Выберите сущность в Hierarchy, затем в Inspector выберите скрипт из зарегистрированных актуальных bindings и нажмите **Add disabled script**. Новый компонент сохраняется выключенным; включить его можно только при актуальной сборке. Команды добавления, включения и удаления поддерживают Undo/Redo, а сохранение сцены записывает компонент в Astro Scene v5. Для подготовки binding сначала обновите состояние сборки C# и зарегистрируйте её в панели Project. Это authoring существующих привязок, не встроенный редактор C# кода. Ограничения и тесты: [отчёт 04.14](docs/reports/04.14-scriptbehavior-editor-authoring.md).

## 04.15 — C# Code workspace

Откройте View → **C# Code**. Панель показывает `.cs` файлы проекта в `Scripts/`; поле пути указывает путь относительно этой папки. Создание, открытие и атомарное сохранение поддерживают UTF-8, а переключение изменённого документа спрашивает о сохранении/отмене. Кнопка сборки запускает существующий offline C# build pipeline; диагностика компилятора открывает файл и переводит каретку на указанную строку. Код остаётся доверенным in-process кодом: подтверждение Play не является sandbox. Workspace ограничивает файлы папкой `Scripts` и принимает исходники размером до 4 MiB. Подсветки, IntelliSense, автодополнения и отладчика нет. См. [отчёт 04.15](docs/reports/04.15-csharp-code-workspace.md).

В C# Code workspace можно открыть **Build settings** и настроить проектный `.astro/csharp-build.json`: SDK 10.0.x, имя сборки и bindings `Scripts/**/*.cs` → managed type. Framework пока фиксирован на `net10.0`; сохранение выполняется отдельно. См. [отчёт 04.16](docs/reports/04.16-csharp-build-manifest-editor.md).

В панели **Project → C# scripts** диагностика различает отсутствующий/несовпадающий binding, устаревший source hash и другую generation сборки. После успешной сборки используйте **Register current C# build** для явной синхронизации AssetRegistry; AssetId исходника сохраняется. Это проверка build/index metadata. CLR тип проверяется только при явном одобренном Play-запуске. Подробности: [C# scripting API](docs/CSHARP_SCRIPTING_API.md), [04.17](docs/reports/04.17-csharp-binding-diagnostics.md).

### C# metadata diagnostics after 04.20

In Project → C# scripts, **Inspect registered C# types** runs an explicit asynchronous metadata check for a current registered build. The worker is a separate AppContainer process; the Editor confines the selected DLL to canonical `Library/Scripts` and blocks concurrent C# builds while it runs. It inspects metadata and does not load or execute user script code. This is Windows-only and not a security certification for hostile PE files; the C# Play host remains trusted-code-only in-process. See [C# scripting API](docs/CSHARP_SCRIPTING_API.md), [D085](docs/DECISIONS.md#d085--project-panel-c-metadata-diagnostics-0420), and [04.20 report](docs/reports/04.20-csharp-project-metadata-diagnostics.md).

### C# metadata batch performance (04.21)

The validator checks up to 64 registered types in one isolated worker session; the Editor can split a project inspection of up to 256 types across sessions. The aggregated scrollable report is capped at 128 KiB and shows type/batch counts. In a local synthetic 256-type Release profile, a 64-type worker session took about 1.35 s median; four 16-type sessions previously took about 5.48 s for the same 64 types. Runtime staging still repeats for each batch. These figures measure this Windows host and ABI-shaped fixture, not a user's project. See [benchmark instructions](docs/TESTING.md#04.21--c-metadata-batch-profile) and [report 04.21](docs/reports/04.21-csharp-metadata-batch-performance.md).
# Campaign save migration status

## 06.57 — macOS/POSIX durability assessment

The copy-aside implementation uses Darwin-supported descriptor-relative file operations, but macOS has not been built or tested in this workspace. POSIX tests ran on WSL/Ubuntu only. Apple documents ordinary `fsync` as insufficient to guarantee physical persistence through a crash or power loss; exact guarantees depend on the filesystem and storage. See [06.57 assessment](docs/reports/06.57-posix-macos-portability-assessment.md), [D120](docs/DECISIONS.md#d120--macos-posix-copy-aside-portability-assessment-0657), and [save-system notes](docs/SAVE_SYSTEM.md#macosposix-durability-assessment-0657).

Campaign progress is namespaced by project settings and campaign ID. Renaming the campaign selects another namespace, and the current save adapter rejects payloads belonging to a different campaign ID. There is no automatic migration or file-copy tool. A future same-ID transfer needs a read-only preview and explicit approval before any user save files are written; see [06.34 assessment](docs/reports/06.34-campaign-save-transfer-assessment.md).

The Editor's Puzzle Save Settings supports same-ID campaign save copying after a read-only preview, with explicit confirmation and exclusive no-overwrite writes. A versioned journal reports transfers interrupted by process exit. The Editor can explicitly resume a valid interrupted transfer after rechecking the journal and source artifacts: complete targets are preserved and skipped, and only missing targets are created. Changed sources and partial, changed, or unsafe targets block resume; partial files remain untouched and require manual inspection. Invalid journals are categorized without inferring target files. On Windows, copy/resume/archive/acknowledgement coordinate through an exclusive `LockFileEx` journal lease; POSIX uses no-follow opens, advisory `flock`, directory-relative identity verification and `unlinkat` for acknowledgement. A readable malformed journal can be copied to an adjacent archive only after separate confirmation; archive creation holds the same source lease, verifies the exact copy, and retains the original for manual review. POSIX locks coordinate only cooperating processes. An interrupted archive write can leave a partial final-path file; retry refuses to replace it and preserves both it and the malformed source journal for manual review. No archive resume or automatic cleanup exists. Archive-name collisions are never replaced; unreadable, unsafe and oversized journals still require manual handling. Save slots are not changed by journal archival or acknowledgement. Copying is not migration and the multi-file write is not atomic across power failure. Tests use isolated temporary fixtures. See [06.36](docs/reports/06.36-campaign-save-transfer-editor-preview.md), [06.37](docs/reports/06.37-campaign-save-transfer-preflight.md), [06.38](docs/reports/06.38-campaign-save-copy-transaction.md), [06.39](docs/reports/06.39-campaign-save-transfer-editor-confirmation.md), [06.40](docs/reports/06.40-campaign-save-transfer-recovery-journal.md), [06.41](docs/reports/06.41-campaign-save-transfer-resume.md), [06.42](docs/reports/06.42-partial-campaign-save-transfer-policy.md), [06.43](docs/reports/06.43-invalid-campaign-transfer-journal-diagnostics.md), [06.44](docs/reports/06.44-malformed-campaign-journal-archive.md), [06.45](docs/reports/06.45-race-safe-malformed-journal-archive.md), [06.47](docs/reports/06.47-posix-journal-acknowledgement-readiness.md), [06.48](docs/reports/06.48-malformed-journal-archive-lock.md), and [06.49 assessment](docs/reports/06.49-partial-journal-archive-assessment.md).

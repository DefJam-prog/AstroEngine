# AstroEngine

Начало разработки модульного 2D/3D-движка на C++20. Headless-приложение выводит информацию о запуске. В `astro_core` есть базовые сервисы, ModuleRegistry, typed EventBus, TimeService/fixed timestep, typed CommandRegistry и in-memory транзакции/Undo/Redo. Optional Windows editor показывает Hierarchy, Inspector с Name и SpriteRenderer, сохраняет Astro Scene v4 и показывает WIC image preview. Core API поддерживает nested prefabs, overrides и обновление экземпляров; UI управления префабами ещё нет. Общего игрового renderer пока нет; есть необязательный UI-демо backend.

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

### Сцены редактора (02.4)

В редакторе доступны сохранение/открытие сцены (Ctrl+S, Ctrl+O), Undo/Redo (Ctrl+Z, Ctrl+Y или Ctrl+Shift+Z) и панель Diagnostics. Astro Scene v4 сохраняет ID/имена сцены и сущностей, иерархию, Transform, SpriteRenderer AssetId и prefab instance metadata. Загрузчик также читает v1–v3. При несохранённых правках переключение проекта/сцены блокируется. Формат и миграции описаны в [FILE_FORMATS.md](docs/FILE_FORMATS.md).

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

Подэтап 05.2: `GameUiModel` управляет меню, запуском/паузой `RuntimeSession`, HUD и настройками. 05.3 добавляет match-three rules, scalar animation sampling и optional SDL3 cues; 05.4 — selective save slots; 05.5 — runtime checkpoint triggers и respawn поверх явно выбранного SaveProfile. Визуальная головоломка, editor authoring и расширенные health/inventory policies ещё отсутствуют. 05.6 упаковывает лишь текущий Game UI demo; это не экспорт законченной игры. См. [Save System и Checkpoints](docs/SAVE_SYSTEM.md), [формат](docs/FILE_FORMATS.md) и [отчёт 05.5](docs/reports/05.5.md).

Подэтап 05.6 добавляет упаковку существующего Game UI runtime demo в Windows ZIP с README и лицензиями SDL3/ImGui, плюс smoke-запуск из распакованного архива. Это поставка UI-прототипа, а не законченной игры: визуальная доска головоломки, уровни и подключённое сохранение прогресса ещё не реализованы. Инструкции: [Windows runtime packaging](docs/BUILD_GUIDE.md#упаковка-windows-runtime-этап-056), ограничения и результаты: [отчёт 05.6](docs/reports/05.6.md).

# AstroEngine

Начало разработки модульного 2D/3D-движка на C++20. Headless-приложение выводит информацию о запуске. В `astro_core` есть базовые сервисы, ModuleRegistry, typed EventBus, TimeService/fixed timestep, typed CommandRegistry и in-memory транзакции/Undo/Redo. Optional Windows editor показывает Hierarchy, structural Scene View и Inspector с редактированием Name через `entity.set_property`; сцены поддерживают сохранение Scene/Entity и Transform. Игровые системы ещё не реализованы.

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

Ожидаемый вывод (ещё не подтверждён запуском в этом окружении):

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

Основная headless-сборка не требует внешних библиотек. Optional Windows editor и экспериментальный графический spike используют локальные SDL3/bgfx/ImGui из build-кэша. Runtime-графика, физика, AI API и платформы кроме Windows пока не реализованы. Лицензия проекта не выбрана.

См. [архитектуру](docs/ARCHITECTURE.md), [план](docs/ROADMAP.md), [статус](docs/STATUS.md), [решения](docs/DECISIONS.md), [проверки](docs/TESTING.md) и [отчёт 00.1](docs/reports/00.1.md).

Редактор 02.1: [запуск и ограничения](docs/reports/02.1.md). EXE: build/stack-spike/editor/astro_editor.exe.
Редактор 02.2: [модель сцены, Hierarchy и Scene View](docs/reports/02.2.md). Панели позволяют создавать сцену, root/child сущности, выбирать и удалять поддеревья. Scene View показывает структуру, не пространственные объекты.
Редактор 02.3: [Inspector и schema properties](docs/reports/02.3.md). Сейчас схема включает обязательное имя сущности и read-only ID; команда изменения проверяет UTF-8 и ограничение 240 байт.

### Сцены редактора (02.4)

В редакторе доступны сохранение/открытие сцены (Ctrl+S, Ctrl+O), Undo/Redo (Ctrl+Z, Ctrl+Y или Ctrl+Shift+Z) и панель Diagnostics. Astro Scene v2 сохраняет ID/имена сцены и сущностей, иерархию, ID компонентов и Transform. При несохранённых правках переключение проекта/сцены блокируется. Формат и миграция v1 описаны в [FILE_FORMATS.md](docs/FILE_FORMATS.md).

Каждая новая сущность получает обязательный Transform с позицией, углами Эйлера в градусах и масштабом. Доступен C++ API `scene.transform(entityId)` и `scene.setTransform(entityId, value)`; значения должны быть конечными числами. Пространственный рендеринг и вычисление мировых матриц пока отсутствуют.

# Сборка минимального приложения

Проект использует C++20, CMake 3.20+ и CTest. Preset windows-debug требует Ninja и предварительно настроенного компилятора. Для MSVC нужен C++ toolchain с Windows SDK и Developer PowerShell x64. Установка инструментов требует отдельного согласования; проект ничего не скачивает.

Для MSVC CMake задаёт статический CRT согласованно для проекта (`/MT` в Release, `/MTd` в Debug), так как optional editor использует предсобранные статические SDL3/bgfx библиотеки. Зависимые библиотеки и targets должны использовать совместимую runtime-конфигурацию.

## Команды Windows 11

Из корня репозитория в подготовленном терминале:

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug
& '.\build\windows-debug\engine\runtime\astro_headless.exe'
```

Останавливайтесь при ненулевом коде команды. CTest не собирает приложение: сначала необходима успешная сборка. Лог тестов находится в `build/windows-debug/Testing/Temporary/LastTest.log`.

## Чистая сборка без удаления файлов

Используйте новый каталог; команда ниже останавливается, если он уже существует:

```powershell
if (Test-Path '.\build\acceptance-00.1') { throw 'Choose a new empty build directory' }
cmake -S . -B build/acceptance-00.1 -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build/acceptance-00.1
ctest --test-dir build/acceptance-00.1 --output-on-failure --no-tests=error
& '.\build\acceptance-00.1\engine\runtime\astro_headless.exe'
```

Выполняйте команды последовательно, проверяя успешное завершение. Генерация внутри каталога исходников запрещена. Пользовательские presets следует хранить в игнорируемом CMakeUserPresets.json.

## Локальные проверки 00.2

```powershell
pwsh -NoProfile -File scripts/verify.ps1
```

Скрипт требует PowerShell 7+, Git, CMake/CTest, Ninja, настроенный C++20-компилятор и clang-format в PATH. Он каждый раз создаёт новый build/verification/<UTC-время>-<ID>, проверяет форматирование без изменения исходников, запускает тесты PowerShell, конфигурирует чистую Debug-сборку, собирает и запускает CTest. После неудачной конфигурации или сборки зависимые шаги помечаются blocked.

report.json содержит статусы, точные аргументы процессов, пути инструментов и их версии, Git HEAD/status, SHA-256 исходников/скриптов, длительности и stdout/stderr. Логи каждого процесса также сохраняются отдельными файлами. Уникальные время/ID/длительности намеренно отличаются между запусками: воспроизводимы процедура и данные для диагностики, не байты отчёта.

Коды скрипта: 0 — обязательные проверки passed, 1 — failed/error/timeout, 2 — incomplete (есть blocked). Отсутствие clang-format блокирует полную проверку. Отсутствие опционального clang-tidy отмечается skipped; если он найден, включается ASTRO_ENABLE_CLANG_TIDY и анализ выполняется во время компиляции. При провале сборки результат анализа консервативно считается неуспешным; подробности — в логе сборки.

Тайм-аут отдельного процесса по умолчанию 300 секунд, настраивается `-TimeoutSeconds`; при превышении завершается дерево процесса. Вывод читается как UTF-8. Для старых инструментов, выводящих локальную кодовую страницу, читаемость не-ASCII-диагностики не подтверждена. Скрипт не является sandbox и запускает только локально доверенные инструменты/тесты.

Формат C++ задаёт .clang-format, общие настройки редактора — .editorconfig. Анализ .clang-tidy включает clang-analyzer и bugprone. Версии этих инструментов записываются в отчёт, но пока не проверены и не закреплены. Запуск вручную: `cmake --preset windows-debug -DASTRO_ENABLE_CLANG_TIDY=ON`; при отсутствии clang-tidy конфигурация должна завершиться ошибкой.

CI workflow в 00.2 не добавлен: реализован локальный вариант проверки. Для его использования в CI нужны подготовленный toolchain и запуск той же команды. Сетевые установки и внешние CI-запуски не выполнялись.

## Возможности и ограничения

`-DBUILD_TESTING=OFF` исключает регистрацию тестов. Сборка не требует сети после подготовки toolchain. UTF-8 включён для MSVC; пути в командах теста заключены в кавычки. Фактическая сборка в путях с пробелами и Unicode ещё не проверена.

Optional `astro_runtime_animation` (scalar track and timeline playback model) is enabled by `-DASTRO_BUILD_ANIMATION_RUNTIME=ON`; it has no SDL/bgfx/ImGui dependency. `ASTRO_BUILD_PUZZLE_RUNTIME=ON` also builds/links this target to preserve the earlier puzzle target's animation dependency. The editor and test configurations enable it automatically.

`astro_runtime_puzzle_save` is a separate optional adapter with no GUI or graphics dependency. Build it for a game by enabling both `-DASTRO_BUILD_PUZZLE_RUNTIME=ON` and `-DASTRO_BUILD_SAVE_RUNTIME=ON`; `BUILD_TESTING=ON` enables it and its integration test automatically. The Windows Editor's `Play Puzzle` mode requires `-DASTRO_BUILD_PUZZLE_RUNTIME=ON` when tests are disabled; when Editor is also enabled, its SaveStore adapter is linked automatically. Per-project preview settings are stored in `.astro/puzzle-save.settings`; progress slots use the user-writable SDL preference root plus the project's configured relative subdirectory. The rules-only Puzzle module stays independent of the file-backed SaveStore.

The Puzzle runtime target also contains the standalone level-definition/file API (`PuzzleLevelDefinition`, `PuzzleLevelFile`) with no additional library dependency. Editor's **Puzzle Level Authoring** panel is available in the `Play Puzzle` Game View; its project catalog supports listing, opening, duplication, and confirmed ID rename. New IDs refuse collisions. Authored-level Save/Load uses the selected level identity and stores each level's manual slots below its project-specific SDL user-data directory. Format, GUI bounds and limitations are documented in [PUZZLE_LEVELS.md](PUZZLE_LEVELS.md) and [FILE_FORMATS.md](FILE_FORMATS.md#puzzle-level-v1-0618); results: [06.18](reports/06.18-puzzle-level-authoring.md), [06.19](reports/06.19-level-scoped-progress.md), and [06.20](reports/06.20-puzzle-level-catalog.md).

В этой среде CMake/CTest/Ninja и MSVC доступны через Visual Studio 2022 Developer Command Prompt x64. Фактические результаты текущей сборки и CTest приведены в [отчёте 01.5](reports/01.5.md); поддержку macOS/Android/iOS не проверяли.

## Упаковка Windows runtime (этап 05.6)

Из Visual Studio 2022 Developer PowerShell x64, при уже подготовленном локальном `build/spike-deps` с SDL3 и Dear ImGui:

```powershell
cmake -S . -B build/runtime-package -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DASTRO_BUILD_GAME_UI_DEMO=ON -DASTRO_BUILD_WINDOWS_RUNTIME_PACKAGE=ON
cmake --build build/runtime-package --target package --parallel 4
```

Готовый ZIP: `build/runtime-package/AstroEngine-Stage05.6-Windows-x64.zip`. Распакуйте архив и запустите `astro_game_ui_demo.exe` для seeded Puzzle demo или `launch_puzzle_campaign.cmd` для авторской двухуровневой кампании. Launcher ищет executable и данные через `%~dp0`, поэтому его можно запустить из любого текущего рабочего каталога. Данные примера находятся в `game-data/puzzle-campaign`; README и лицензии SDL3/Dear ImGui включены. Editor, build tools и тестовые ресурсы не включаются. Для другой кампании передайте `--project-root` и `--campaign` напрямую executable. Пользовательские слоты хранятся отдельно в SDL per-user data directory. CMake не загружает зависимости.

Чтобы проверить собранный пакет из чистого каталога, сконфигурируйте отдельную Debug-конфигурацию с `-DBUILD_TESTING=ON` и теми же двумя `ASTRO_BUILD_*` флагами, соберите её, затем выполните:

```powershell
ctest --test-dir build/runtime-package-tests -R runtime.windows.package.clean_start --output-on-failure --no-tests=error
```

CTest создаёт ZIP, распаковывает его в новый уникальный подкаталог, проверяет executable, launcher, README, лицензии и все три файла кампании, запускает `astro_game_ui_demo.exe --smoke`, затем запускает executable против bundled campaign и проверяет smoke сохранения/восстановления. Отдельная ручная проверка запускает `launch_puzzle_campaign.cmd --smoke` из другого рабочего каталога. Проектная лицензия не выбрана; наличие upstream license files не задаёт лицензию AstroEngine.

Архив содержит игровой UI прототип, один пример кампании и ручные слоты campaign progress, а не законченную игру. Произвольный проект не экспортируется, autosave, миграции профиля и checkpoint gameplay integration не реализованы. Перед выпуском продукта нужно завершить оставшуюся игровую интеграцию и отдельно оценить лицензионную политику всего приложения. Проверенные содержимое и ограничения: [отчёт 05.10](reports/05.10-self-contained-campaign-package.md).

## Проверка редактора и файловых сцен (02.4)

Из Visual Studio 2022 Developer PowerShell x64:

```powershell
cmake --build build/stack-spike --parallel 4
ctest --test-dir build/stack-spike --output-on-failure
```

Результаты прогона сохранены в `build/02.4-build.log` и `build/02.4-ctest.log`. Каталог сборки должен быть сконфигурирован заранее; для чистой конфигурации выберите новый каталог и следуйте инструкциям выше.

Подэтап 03.1 проверялся теми же командами. Итоговые логи: `build/03.1-build.log`, `build/03.1-ctest.log`; Windows x64/MSVC Debug, CTest 9/9.

Подэтап 03.3 проверен тем же Windows x64/MSVC Debug preset. CTest может отсутствовать в PATH даже в Developer shell; в данной установке используйте `& 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe' --test-dir build/stack-spike --output-on-failure`. Логи: `build/03.3-build.log`, `build/03.3-ctest.log`. Editor использует встроенный Windows Imaging Component (WIC), внешняя библиотека не добавляется.

Подэтап 03.4 добавляет `astro_prefabs`, который собирается штатной CMake-конфигурацией без внешних зависимостей. Windows x64/MSVC Debug сборка и полный CTest 13/13 прошли; логи `build/03.4-build.log`, `build/03.4-ctest.log`. Отдельно: `ctest --test-dir build/stack-spike -R '^prefab\.' --output-on-failure`.

## Project C# scripts (04.9)

PowerShell 7 and an already available local SDK pinned by the project are required. Create `.astro/csharp-build.json` as described in [CSHARP_SCRIPTING_API.md](CSHARP_SCRIPTING_API.md), then run from the repository root:

```powershell
pwsh -NoProfile -File tools/csharp/build-project.ps1 `
  -ProjectRoot "C:\path\to\game-project" `
  -DotnetRoot "C:\path\to\dotnet-sdk"
pwsh -NoProfile -File tools/csharp/build-project.ps1 `
  -ProjectRoot "C:\path\to\game-project" `
  -DotnetRoot "C:\path\to\dotnet-sdk" -Check
```

Exit codes: 0 = successful/current, 1 = restore/compiler failure, 2 = invalid configuration/tool path, 3 = stale or missing successful output in check mode. Restore has no package sources. The build does not auto-register an AssetId, execute scripts, or sandbox C#.

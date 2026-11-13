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

В этой среде CMake/CTest/Ninja и MSVC доступны через Visual Studio 2022 Developer Command Prompt x64. Фактические результаты текущей сборки и CTest приведены в [отчёте 01.5](reports/01.5.md); поддержку macOS/Android/iOS не проверяли.

## Упаковка Windows runtime (этап 05.6)

Из Visual Studio 2022 Developer PowerShell x64, при уже подготовленном локальном `build/spike-deps` с SDL3 и Dear ImGui:

```powershell
cmake -S . -B build/runtime-package -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DASTRO_BUILD_GAME_UI_DEMO=ON -DASTRO_BUILD_WINDOWS_RUNTIME_PACKAGE=ON
cmake --build build/runtime-package --target package --parallel 4
```

Готовый ZIP: `build/runtime-package/AstroEngine-Stage05.6-Windows-x64.zip`. Распакуйте архив и запустите `astro_game_ui_demo.exe`. Внутри лежат краткое описание и лицензии SDL3/Dear ImGui. Пакет содержит только статически слинкованный demo executable и эти тексты; editor, build tools и исходниковые тестовые ресурсы не включаются. CMake не загружает зависимости.

Чтобы проверить собранный пакет из чистого каталога, сконфигурируйте отдельную Debug-конфигурацию с `-DBUILD_TESTING=ON` и теми же двумя `ASTRO_BUILD_*` флагами, соберите её, затем выполните:

```powershell
ctest --test-dir build/runtime-package-tests -R runtime.windows.package.clean_start --output-on-failure --no-tests=error
```

CTest создаёт ZIP, распаковывает его в новый уникальный подкаталог, проверяет executable/README/license files и запускает `astro_game_ui_demo.exe --smoke` из каталога распаковки. Проектная лицензия не выбрана; наличие upstream license files не задаёт лицензию AstroEngine.

Архив пока содержит Game UI demo, а не законченную игру: в нём нет видимой доски puzzle, уровней и подключённого к UI SaveProfile/checkpoint прогресса. Перед выпуском продукта нужно выполнить оставшуюся игровую интеграцию и отдельно оценить лицензионную политику всего приложения.

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

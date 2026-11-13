# Проверки и критерии готовности функций

## Фактические проверки 01.1

Добавлены unit-проверки `core.unit`: `Result<T>/void`, уровни Logger, фильтрация и sink, конфигурация UTF-8/BOM/CRLF, ошибки отсутствующего ключа/синтаксиса/дубликатов, сохранение старого состояния после неудачного load, round-trip, валидация и EngineContext. Результаты этого подэтапа: [отчёт 01.1](reports/01.1.md).

## Фактические проверки 01.2

`core.unit` дополнен проверками детерминированного топологического порядка, reverse shutdown, запрета повторного lifecycle, отсутствующей/циклической/дублирующей зависимости, rollback после ошибки initialize и продолжения shutdown после ошибки одного модуля. Полный CTest после изменений: 5/5, включая headless smoke и регрессию stack spike; подробности — [отчёт 01.2](reports/01.2.md).

## Фактические проверки 01.3

`core.unit` проверяет typed dispatch, изоляцию типов событий, subscribe/unsubscribe во время dispatch, исключения callbacks, fixed-step count/interpolation, catch-up cap и dropped-time accounting, clamp большого delta, паузу, неверные значения и порядок/error пути module tick. Полный CTest после изменений: 5/5; headless и stack-spike smoke сохранились. Подробности: [отчёт 01.3](reports/01.3.md).

## Фактические проверки 01.4

`core.unit` проверяет регистрацию, ID/schema version, точные payload/result типы, validation-only без execute, отклонение до выполнения, типизированный результат, void-команду, отсутствующий ID, дублирование, ошибки и исключения validator/executor, audit entries. Полный CTest после изменений: 5/5; headless и stack-spike smoke сохранились. Подробности: [отчёт 01.4](reports/01.4.md).

## Фактические проверки 01.5

`core.unit` проверяет атомарное применение списка шагов, rollback префикса при ошибке, очистку истории при ошибке rollback, Undo в обратном и Redo в прямом порядке, redo branch invalidation, восстановление состояния при ошибках Undo/Redo, transactional command через CommandRegistry и undo barrier non-transactional команды. Полный CTest: 5/5; подробности: [отчёт 01.5](reports/01.5.md).

## Фактические проверки 00.2

## Фактические проверки 00.3

- Чистая baseline Windows x64 Debug сборка MSVC: успешна; CTest 3/3.
- Spike SDL3/bgfx/ImGui: сборка успешна; CTest 4/4. `stack_spike.smoke` автоматически проверяет 90 кадров, активный docking, клавиатурное событие, resize окна и наличие draw calls.
- Ручной запуск: SDL 3.5.0, Dear ImGui 1.93.1 WIP, bgfx Direct3D 11; процесс рендерил 18 409 кадров, UI зарегистрировал клавиши и клики. Скриншот/ручное перетаскивание docking не подтверждены.
- Подробные команды, конфигурация инструментов, ревизии и ограничения: [reports/00.3.md](reports/00.3.md).

- `pwsh -NoProfile -File tests/scripts/process_runner.tests.ps1`: 5/5 проверок — stdout/stderr, ненулевой код, отсутствующая программа, тайм-аут, Unicode-аргумент с пробелами. Эти тесты проверяют инфраструктуру процессов, а не сборку движка.
- `pwsh -NoProfile -File scripts/verify.ps1`: JSON и логи созданы, exit 2 / incomplete. Конфигурация, сборка, CTest и форматирование blocked из-за отсутствия инструментов; статический анализ skipped.
- `pwsh -NoProfile -File tests/scripts/report.tests.ps1 -ReportPath <путь-к-report.json>`: 9/9 проверок схемы, исходных хэшей, этапов, итогового статуса и наличия логов прошли на реальном отчёте.
- Добавлены CTest-сценарии headless.help и headless.invalid, сохранён headless.startup. Выполненных C++/CTest-тестов: 0; компилятор и CMake/CTest недоступны. Обработка сбоев вывода/исключений приложения ещё не проверена исполнением.

См. [reports/00.2.md](reports/00.2.md). Сценарии успешной C++-сборки и запуска clang-format/clang-tidy пока не подтверждены. Подмены build-инструментов для заявления об успешной сборке не используются.

Исторически, при выполнении 00.1, CTest smoke-тест `headless.startup` ещё не был запущен из-за отсутствия toolchain. Сейчас он исполняется в регрессионном наборе. Тест проверяет запуск приложения, нулевой код завершения, отсутствие stderr и обязательную информацию о запуске; проверки не зависят от assert/NDEBUG.

Команда: `ctest --preset windows-debug` после конфигурации и сборки по [BUILD_GUIDE.md](BUILD_GUIDE.md). Результаты первоначальной проверки среды: [reports/00.1.md](reports/00.1.md). Теперь unit-тесты ядра и Scene Model, integration-команд и smoke-тесты редактора/runtime выполняются; сохранение сцен и игровые системы ещё не реализованы.

## Проверки каждого этапа

| Уровень | Область |
| --- | --- |
| Unit | Ядро и Scene Model |
| Integration | Команды; сериализация сцен появится после реализации |
| Smoke | Редактор и runtime |
| Regression | Ранее готовые функции |

Для каждого этапа указывать результаты этих групп проверок. Если соответствующая подсистема ещё отсутствует или проверку выполнить невозможно, явно записывать причину; отсутствие теста не считать успехом. Новые функции покрываются подходящими тестами, ранее готовые проверяются на регрессии.

Проверки дополняют критерии приёмки из [ROADMAP.md](ROADMAP.md). Учитывать чистую сборку, Windows-пути, Unicode, UTF-8 и отсутствие сети. В текущей среде CMake/CTest/Ninja/MSVC доступны из Visual Studio 2022 Developer PowerShell x64. Чистые сборки и другие платформы проверяются отдельно и не считаются покрытыми инкрементальным тестовым каталогом.

## Графика

Использовать визуальные эталонные снимки с допустимыми отклонениями. Метрика сравнения и численные допуски пока не определены. Для воспроизводимости фиксировать тестовую сцену, настройки и окружение захвата; сохранять результат сравнения и выявленные отличия. Изменение эталона не должно скрывать необъяснённую регрессию.

## Производительность

Использовать фиксированные тестовые сцены и измерять FPS/frame time, память, загрузку и размер сборки. Фиксировать окружение, настройки и метод измерения, чтобы сравнения были воспроизводимыми.

Целевые бюджеты утверждаются отдельно. Пока бюджет не утверждён, измерение является наблюдением, а не подтверждением достижения целевой производительности.

## Сохранения

Обязательны round-trip сохранения/загрузки, миграции, прерывание записи, неверная версия, отсутствующие объекты, повторная загрузка и проверка checkpoint.

Проверки дополняют [SAVE_SYSTEM.md](SAVE_SYSTEM.md): выборочная запись, повреждённые файлы, резервное восстановление, смерть/respawn, перезапуск игры и стабильные runtime-ID динамических объектов. Проверять сохранность выбранных данных и отсутствие исключённых полей в сохранении.

## Безопасность и зависимости

Проверять валидацию путей, границы памяти, обработку недоверенных ассетов и скриптов, права AI и лицензии сторонних библиотек. Конкретные средства анализа и тестирования выбираются при реализации соответствующего подэтапа; они пока не установлены и не настроены.

Для AI учитывать ограничения из [AI_INTEGRATION.md](AI_INTEGRATION.md). Проверка лицензий документируется отдельно от результатов исполняемых тестов. Наличие проверок не гарантирует отсутствие ошибок или уязвимостей.

## Обязательная документация

Целевой комплект документов:

- README.md в корне проекта.
- docs/ARCHITECTURE.md.
- docs/ROADMAP.md.
- docs/STATUS.md.
- docs/DECISIONS.md.
- docs/TESTING.md.
- docs/COMMAND_API.md.
- docs/FILE_FORMATS.md.
- docs/SAVE_SYSTEM.md.
- docs/TEMPLATE_GUIDE.md.
- docs/BUILD_GUIDE.md.

Это перечень обязательных документов, а не утверждение об их полноте. COMMAND_API создан в подэтапе 01.4; FILE_FORMATS и TEMPLATE_GUIDE пока отсутствуют. Остальные документы также должны получать содержательное наполнение по мере реализации соответствующих функций.

## Готовность функции и отчёт

Каждая готовая функция должна иметь документацию пользователя, пример, тест и известные ограничения. Одного описания запланированной функции недостаточно.

В отчёте подэтапа указывать изменённые файлы, работающее поведение, команды и окружение проверки, результаты тестов, пропущенные проверки с причинами, известные проблемы и следующий рекомендуемый подэтап. После отчёта остановиться до ручного подтверждения пользователя.

## Подэтап 02.1
Добавлен optional astro_editor: окно, docking, меню и project.astro. CTest 6/6 на момент завершения 02.1. Команды и ограничения: [отчёт 02.1](reports/02.1.md).

### Проверка безопасности project.astro (исправление 02.1)

`editor.smoke` проверяет round-trip UTF-8 имени с Ω, отказ создавать проект в существующем каталоге с контрольным файлом без изменения его содержимого и сохранение текущего проекта после открытия файла с неподдерживаемой версией. Это интеграционная smoke-проверка редактора, а не отдельный unit-тест формата.

## Подэтап 02.2

`scene.unit` проверяет UTF-8 имена, создание сущностей и parent links, отклонение пустых имён, неизвестных родителей, циклов и удаления родителя без явного recursive-флага, а также удаление поддерева без висячих ссылок. `editor.smoke` проверяет модель с root/child entity, окно редактора, docking Scene View/Hierarchy и графический вывод. Полный CTest после изменений: 7/7. Сохранение сцены и визуальный image-diff пока не реализованы.

## Подэтап 02.3

`scene.unit` проверяет предел длины и invalid UTF-8 при создании сущностей. `scene.command.integration` проверяет схему Name/ID, default/required/read-only metadata, validation-only без изменения модели, command execution, UTF-8 round-trip, неверный тип/ID/версию, пустое/слишком длинное/некорректное значение и отсутствие частичного изменения при отказе. `editor.smoke` проверяет регистрацию команды и применение тестового редактирования через `CommandRegistry`, а также docking и рендеринг Inspector. Регрессия полного CTest: 8/8. Ручное редактирование мышью и image-diff не выполнялись.

## Подэтап 02.4 — сцены и редактор

Интеграционный тест `scene.file.integration` проверяет UTF-8 round-trip, иерархию, продолжение ID после удаления, атомарную замену существующего файла, неверную версию, отсутствующего родителя, цикл, лишние данные, повреждённый UTF-8 и отсутствующий файл. `editor.smoke` проверяет сохранение/открытие, Undo/Redo и то, что открытие файла с неподдерживаемой версией не заменяет активную сцену. Это автоматический smoke, не ручная проверка визуального поведения GUI.

Проверенная Windows x64 конфигурация: Visual Studio 2022 MSVC, Debug, локальные SDL3/bgfx/ImGui. Команды из Developer PowerShell:

```powershell
cmake --build build/stack-spike --parallel 4
ctest --test-dir build/stack-spike --output-on-failure
```

Последний прогон: сборка успешна (`ninja: no work to do`), CTest 9/9 passed. Сериализация не проверяет принудительное прерывание записи и не содержит миграций/резервной копии.

## Подэтап 03.2 — Asset Registry и импорт

`assets.hash.unit` проверяет SHA-256 по эталонным векторам пустой строки и `abc`. `assets.registry.integration` создаёт отдельный временный project и проверяет PNG/JPEG metadata, UTF-8 source path, stable ID и индекс после reopen, CRLF, dedup повторного импорта, явный reimport при смене содержимого, missing source report, восстановление повреждённого cache entry, rejection traversal/unsupported/corrupt PNG, missing ID и неизменность исходного файла/повреждённого индекса. Symlink cache проверяется, если ОС разрешает создать ссылку без дополнительной привилегии. Полный CTest после реализации: 11/11.

## Подэтап 03.1 — Transform и миграция схемы

`scene.unit` проверяет обязательный Transform, default scale, Component ID lookup, принятие конечных значений и отклонение бесконечности. `scene.file.integration` проверяет точный round-trip Transform и Component ID, чтение Astro Scene v1 с default Transform, запись мигрированного документа как v2 с сохранением нового ID, и диагностику неизвестного компонента. Полный регрессионный CTest: 9/9.

## 03.3 результаты

Windows x64, MSVC 14.44, Debug, BGFX D3D11: `cmake --build build/stack-spike --parallel 4` успешно; CTest 11/11 успешно (`build/03.3-build.log`, `build/03.3-ctest.log`). Покрыты SpriteRenderer command validation/Undo/Redo и Scene v3 serialization. `editor.smoke` импортирует собственный PNG, проверяет WIC decode/BGFX texture upload и scene round-trip, плюс запуск UI и project/scene regression. Визуальный pixel-diff эталон не снимается.

## 03.4 результаты

Полная Windows x64/MSVC Debug сборка успешна; CTest 13/13. `prefab.unit` покрывает локальные ID, parent, root и hierarchy cycle validation. `prefab.integration` проверяет file round-trip, nested instantiate, graph cycle rejection без изменения Scene, Scene v4 persistence, command Undo/Redo, prefab source update, сохранение EntityId и override, повторное update после загрузки. См. `build/03.4-build.log` и `build/03.4-ctest.log`. Нет отдельного editor smoke для prefab authoring, так как UI в 03.4 не добавлялся.

## 03.5 результаты

`assets.registry.integration` теперь проверяет graph save/load, cycle rejection, closure, упаковку reachable assets, повторное использование cache, ремонт повреждённого артефакта и различие target tag. Полная MSVC Debug сборка прошла; CTest 13/13. `build/03.5-build.log` и `build/03.5-ctest.log`. Не проверялись межплатформенная сборка, декодирование/перекодирование текстур и визуальные golden images: код сохраняет байты исходных image payload.



## 04.1 результаты

`core.unit` покрывает fixed-step TimeService и paused manual step. `runtime.session.unit` проверяет копирование Scene, неизменность authoring state, переходы, число шагов, Pause/Resume/Step/Stop, ошибки Result и исключения callback. Windows x64/MSVC Debug сборка успешна; полный CTest: 14/14. `editor.smoke` прошёл регрессионно, но Play UI не реализован.

## 04.2 результаты

`input.mapping.unit` проверяет bindings, alternative sources, signed axes, deadzone, digital edge transitions, frame reset, transient mouse delta, invalid controls and device clearing. `input.sdl.adapter.smoke` injects SDL3 key, mouse, wheel, gamepad button/axis, removal, and window-focus-loss events without physical hardware. Windows x64/MSVC Debug build successful; full CTest **16/16**. `editor.smoke` passed regression. Physical keyboard/mouse/controller interaction and non-Windows adapters were not tested.

## 04.3 результаты

`camera.rig.unit` проверяет Custom pose, mode presets, target follow, frame-delta smoothing, zero/invalid delta, bounds, zoom clamp, rotation inheritance, missing target and invalid settings without changing pose. Полный CTest — 17/17; `editor.smoke`/graphics regression прошли, но camera image output отсутствует, так как renderer ещё не подключён.

## 04.4 результаты

`runtime.interaction.integration` проверяет Requested/Activated/Rejected event order/sequence, registered/duplicate/missing behaviors, enabled-state, in/out-of-range activation, callback mutation isolation on Runtime Scene, Result error and thrown exception diagnostics. Полный Windows x64/MSVC CTest — 18/18; editor/graphics smoke прошли regression, но визуальный interaction test не выполнялся.

## 05.1 результаты

`runtime.physics2d.integration` проверяет gravity/fixed-step movement, solid floor correction, velocity stop, Enter/Stay/Exit sequence, trigger overlap without correction, invalid values, static velocity rejection, one-SceneId binding, wrong-scene rejection and authoring/runtime Scene isolation. Полный Windows x64/MSVC CTest — 19/19; editor/graphics smoke regressions passed. Нагрузочное масштабирование, CCD/tunneling scenarios and visual collision rendering are not tested or implemented. Logs: `build/05.1-build.log`, `build/05.1-ctest.log`.

## 05.2 результаты

`runtime.game_ui.unit` проверяет экранные действия, UTF-8/ID validation, HUD CRUD, bool/number/choice settings, диапазоны, choices и typed setting event. `runtime.game_ui.integration` проверяет запуск/пауза/возобновление RuntimeSession через UI model, fixed-step suppression, переходы Settings и возврат в меню без изменения authoring Scene. `runtime.game_ui.visual.smoke` создаёт скрытое SDL3 окно, рисует шесть экранных кадров, проверяет ImGui vertices и сравнивает main-menu BMP с эталоном: MAE ≤1.5/255; не более 1% пикселей могут отличаться более чем на 12/255 по каналу. Полный Windows x64/MSVC CTest с `ASTRO_BUILD_GAME_UI_DEMO=ON` и включёнными graphics smoke: **22/22**. Отдельная чистая headless-конфигурация (без Editor/SDL3/ImGui) собралась с нуля и прошла **18/18** тестов; в ней работают два новых теста backend-neutral модели, а graphics smoke отсутствуют. Другие экраны не имеют pixel-golden эталонов; ручной UX review не выполнялся. Логи: `build/05.2-baseline-build.log`, `build/05.2-baseline-ctest.log`, `build/05.2-configure.log`, `build/05.2-build.log`, `build/05.2-ctest.log`, `build/05.2-headless-configure.log`, `build/05.2-headless-build.log`, `build/05.2-headless-ctest.log`.

## 05.3 результаты

`runtime.puzzle.unit` проверяет границы размера/палитры, детерминированный board, invalid moves без мутации, legal match resolution, cascades, refill, points/level thresholds; также проверяет animation keyframe validation, endpoint clamping и linear interpolation. `runtime.audio.sdl.smoke` открывает SDL3 dummy playback stream и отправляет синтезированный cue. Полная Windows x64/MSVC Debug сборка `build/stack-spike` успешна; CTest **24/24** прошёл (включая `stack_spike.smoke`, `runtime.game_ui.visual.smoke`, `runtime.audio.sdl.smoke`, `editor.smoke`). Чистый headless configure/build без SDL/ImGui прошёл; CTest **19/19**. Базовая проверка перед изменением: 22/22. Физические speakers, visual puzzle interaction, другие audio backends и pixel-golden для игровой доски не проверялись. Логи: `build/05.3-build.log`, `build/05.3-ctest.log`, `build/05.3-headless-configure.log`, `build/05.3-headless-build.log`, `build/05.3-headless-ctest.log`.

## 05.4 результаты

`runtime.save.integration` проверяет explicit field selection/исключения, UTF-8 round-trip, numeric/bool values, slot bounds/metadata, reopen store, repeated load idempotence, profile fingerprint/version и game version rejection without Scene mutation, missing entities/components, bad checksum, unsupported format version, backup fallback and orphan temporary file. Windows test additionally blocks destination replacement by a handle without delete-sharing, then confirms the prior committed slot remains loadable. Baseline MSVC x64 Debug was clean: optional profile CTest **24/24**, headless **19/19**. After implementation, optional profile build and full CTest **25/25**; headless build and CTest **20/20**. No process was killed during actual disk I/O; interruption was simulated by a blocked atomic replacement and an abandoned partial temp artifact. Schema migrations remain deferred; v1 rejects unsupported versions. Checkpoint/respawn behavior is covered by 05.5 below. Logs: `build/05.4-baseline-build.log`, `build/05.4-baseline-ctest.log`, `build/05.4-baseline-headless-build.log`, `build/05.4-baseline-headless-ctest.log`, `build/05.4-build.log`, `build/05.4-ctest.log`, `build/05.4-headless-build.log`, `build/05.4-headless-ctest.log`.

## 05.5 результаты

`runtime.checkpoint.integration` проверяет XY AABB boundary, actor entry/exit, edge-triggering, one-shot consumption, condition rejection/retry after re-entry, duplicate/missing registrations, active checkpoint selection/unregister, fallback respawn, and authoring/runtime Scene isolation. It also verifies SaveSelectedProfile activation requires a valid context, saves only that profile's fields, restores selected values to the Play Scene before assigning the spawn point, and distinguishes PositionOnly from SelectedProfile respawn. Baseline MSVC x64 Debug: optional **25/25**, headless **20/20**. Final Windows x64/MSVC build and CTest: optional **26/26**, headless **21/21**. A separate production configuration with `BUILD_TESTING=OFF` and `ASTRO_BUILD_CHECKPOINT_RUNTIME=ON` compiled `astro_runtime_checkpoint` successfully without SDL/ImGui. No rendered checkpoint area, physical collision trigger, health/inventory component, process death detection, sound, or editor workflow was tested or implemented. Logs: `build/05.5-baseline-build.log`, `build/05.5-baseline-ctest.log`, `build/05.5-baseline-headless-build.log`, `build/05.5-baseline-headless-ctest.log`, `build/05.5-build.log`, `build/05.5-ctest.log`, `build/05.5-headless-build.log`, `build/05.5-headless-ctest.log`, `build/05.5-runtime-only-configure.log`, `build/05.5-runtime-only-build.log`.

## 05.6 результаты

Baseline before changes, Windows x64/MSVC Debug: optional profile CTest **26/26**; headless **21/21**. `ASTRO_BUILD_WINDOWS_RUNTIME_PACKAGE` requires Windows x64/MSVC and `ASTRO_BUILD_GAME_UI_DEMO=ON`; it packages only `astro_game_ui_demo.exe`, `README-runtime.txt`, and SDL3/Dear ImGui license texts. `runtime.windows.package.clean_start` creates a ZIP, extracts it to a fresh unique directory, checks these files and launches the extracted executable with `--smoke` from that directory. Post-change Debug package profile CTest: **24/24**, including archive smoke. Post-change headless CTest: **21/21**. A separate Release configuration with `BUILD_TESTING=OFF` generated the ZIP, and the same clean-start script passed against it. Release archive size is 1,652,875 bytes and it contains no DLL entries. Logs: `build/05.6-package-configure.log`, `build/05.6-package-build.log`, `build/05.6-package-smoke.log`, `build/05.6-package-ctest.log`, `build/05.6-package-final.log`, `build/05.6-headless-regression.log`, `build/05.6-headless-final.log`, `build/05.6-release-configure.log`, `build/05.6-release-package.log`, `build/05.6-release-final.log`, `build/05.6-release-smoke.log`.

## 06.1 результаты

Windows x64/MSVC Debug baseline перед изменениями был чистым: optional/editor CTest **26/26**, headless **21/21**. После изменений `cmake --build build/stack-spike --parallel 4` прошёл; полный optional/editor CTest — **28/28** (`build/06.1-editor-ctest.log`). Headless build прошёл, CTest — **23/23** (`build/06.1-headless-final.log`). Отдельная Release-конфигурация с `BUILD_TESTING=OFF`, Editor/SDL выключенными и `ASTRO_BUILD_VISUAL_LOGIC=ON` собрала `astro_visual_logic` (`build/06.1-logic-runtime-build.log`). Unit-тесты проверяют типы, валидацию, лимиты, циклы и удаление узлов; integration — детерминированное исполнение, пошаговый trace, ошибку деления на ноль и неполные графы. `editor.smoke` проверяет отображение панели и вычисление встроенного графа; ручная работа мышью по меню не автоматизирована. Граф не сохраняется, поэтому persistence/regression-тестов формата пока нет. Логи baseline: `build/06.1-baseline-editor.log`, `build/06.1-baseline-headless.log`; итоговая сборка: `build/06.1-build.log`.

## 06.2 результаты

Windows x64/MSVC Debug baseline после локальной фиксации 06.1: editor CTest **28/28**, headless **23/23**. После добавления UI document/editor: editor build успешен, полный CTest **30/30** (`build/06.2-editor-ctest.log`); headless build успешен, полный CTest **25/25** (`build/06.2-headless-ctest.log`). `ui.document.unit` проверяет темы, responsive resolution на разных viewports, bounds/UTF-8/element deletion; `ui.binding.integration` проверяет текстовые и видимые Scene bindings, обновление значения, несовместимые ссылки и отсутствие частичных обновлений при stale reference. `editor.smoke` проверяет создание preview model, responsive coordinates, binding refresh и наличие UI Editor panel в отрисованном окне. Pixel-golden для UI Editor и ручная мышиная UX проверка не выполнялись. Нет save/load тестов, так как UI document file persistence не реализована. Logs: `build/06.2-build.log`, `build/06.2-editor-ctest.log`, `build/06.2-headless-build.log`, `build/06.2-headless-ctest.log`.

## 06.3 результаты

Baseline Windows x64/MSVC Debug до изменений: optional/editor CTest **30/30**, headless **25/25**. После изменений Editor build successful, full CTest **31/31**; headless build successful, full CTest **26/26**. `runtime.animation.unit` checks scalar track editing, strict time ordering, finite/magnitude/count bounds, rejected-edit preservation, sample/seek/play/pause/end-stop. Existing `camera.rig.unit` tests cover preset configuration/follow behavior; `editor.smoke` also verifies Timeline panel rendering and ThirdPerson target/pose setup. A separate Release configuration with `BUILD_TESTING=OFF`, `ASTRO_BUILD_ANIMATION_RUNTIME=ON`, and Editor/game UI disabled built `astro_runtime_animation` without SDL/bgfx/ImGui. On the first full CTest invocation, the existing `stack_spike.smoke` did not receive its synthetic key event (`keys=0`); the isolated retry passed, followed by the full 31/31 pass. No pixel-golden or manual camera-view comparison exists because the renderer is not connected. Timeline-to-Scene binding, persistence and game export are not implemented. Logs: `build/06.3-baseline-editor-ctest.log`, `build/06.3-baseline-headless-ctest.log`, `build/06.3-editor-build.log`, `build/06.3-editor-ctest-first.log`, `build/06.3-stack-spike-retry.log`, `build/06.3-editor-ctest.log`, `build/06.3-headless-build.log`, `build/06.3-headless-ctest.log`, `build/06.3-animation-only-configure.log`, `build/06.3-animation-only-build.log`.

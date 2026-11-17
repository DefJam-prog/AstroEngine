# Этапы, подэтапы и критерии приёмки

## Зафиксированное направление продукта

Цель — универсальный модульный движок 2D/3D для разных жанров с графическим редактором и ИИ как ключевым путём авторинга. AI adapter и сетевой провайдер не требуются для обычного запуска движка или игры. Puzzle остаётся сквозным образцом для проверки основы, а не целевым жанром продукта. AI-ready Command API, схемы, транзакции, разрешения и отчёты учитываются в ранних этапах; этап 09 добавляет интеграцию и AI Game Tester, а не впервые вводит архитектуру для ИИ. Это целевая дорожная карта; фактические возможности не считаются готовыми без тестов и отчёта. См. [PRODUCT_REQUIREMENTS.md](PRODUCT_REQUIREMENTS.md) и [решение D067](DECISIONS.md#d067-универсальный-ai-first-профиль-продукта).

## 07.1a — 3D camera/mesh contract and procedural bgfx smoke (implemented prototype)

Added optional backend-neutral `astro_runtime_render3d` with validated right-handed camera view/projection matrices and bounded indexed triangle mesh data, plus a colored procedural cube. The separate Windows D3D11/bgfx smoke uses the existing dependency cache, performs thresholded pixel readback, and reports VSync CPU frame-interval p50/p95. Across three isolated-cube runs, median p50 was 16.735 ms and median p95 32.824 ms (ranges 16.701–16.764 and 32.055–34.120 ms); this is not a target budget or complete scene frame time. All three pixel checks found 85,155 foreground pixels, 169 color buckets and bounds 280,135–658,498; it is not a golden-image comparison. Windows Release build completed 161/161 actions and full CTest passed 46/46 in 13.94 s after readback was added. Scene serialization, Editor Scene/Game View, importer/materials/lighting, and 3D physics remain out of scope. See [3D prototype guide](3D_RENDERING.md), [report](reports/07.1a-3d-camera-mesh-prototype.md), [D123](DECISIONS.md#d123--3d-camera-mesh-prototype-boundary-071a).

## 07.1b — bounded glTF/GLB static mesh importer (implemented prototype)

An opt-in `astro_gltf` target now parses `.gltf` and `.glb` geometry through pinned fastgltf v1.0.1 with locally staged, hash-verified simdjson 5.1.0 and simdutf 9.3.3; implicit CMake downloads are disabled. The initial output is backend-neutral static triangle meshes with POSITION and optional indices. Model paths and external buffers are constrained to regular files within project `Assets`; source assets remain untouched and checked geometry caches are generated under `Library/Derived/gltf`. Windows x64 Release build: 95/95 actions; full CTest: 42/42 in 8.92 s. A separate default headless Release build succeeded with the importer disabled and no importer target. This does not yet load meshes into Scene/AssetRegistry/Editor, and it does not import materials, images, UVs, normals, node transforms, animation, skinning, morphs or compression. No 3D physics adapter has been selected. Fuzz/sanitizer, macOS and mobile validation remain pending. See [import guide](3D_RENDERING.md), [asset pipeline](ASSET_PIPELINE.md#optional-gltf-geometry-import-071b), [report](reports/07.1b-bounded-gltf-importer.md), and [D125](DECISIONS.md#d125--optional-bounded-gltf-static-mesh-importer-071b).

## 07.1c — Scene model asset references (implemented prototype)

Scene v6 adds a typed `MeshRenderer` component referencing a stable Model AssetId, with mesh/primitive selection and visibility. Asset index v3 adds bounded `.gltf`/`.glb` records with source size/hash and importer content digest; `GltfImporter::importAndRegister` connects successful import to registry registration. The typed `entity.set_mesh_renderer` command supports validation, execution, audit and undo/redo. Legacy Scene v1–v5 and Asset index v1–v2 remain readable and are written in the current schema only after explicit save/registration. Windows x64 Release clean build and full CTest results are recorded in [testing](TESTING.md#071c-scene-model-asset-references) and the [substage report](reports/07.1c-scene-model-asset-reference.md). This is data/command integration only: Editor/Game View rendering, prefab serialization, per-buffer dependency records, materials, normals, UVs and transform/node import are outside 07.1c.

## 07.1d — Scene MeshRenderer resolution (implemented prototype)

The optional `astro_gltf` module now resolves a Scene `MeshRendererComponent` to its selected backend-neutral primitive. Resolution checks the registered AssetId and source fingerprint, imports/loads current geometry, compares the persisted importer digest (including external buffers), and validates mesh and primitive indices. Stale or invalid references fail with a diagnostic; explicit reimport/register is needed to refresh changed content. The resolver returns geometry but performs no draw call and does not connect Scene/Game View. See [testing](TESTING.md#071d-scene-meshrenderer-resolution), the [3D guide](3D_RENDERING.md#scene-mesh-resolution-071d), and [report](reports/07.1d-scene-mesh-resolution.md).

## 07.1e — Standalone Scene Mesh BGFX preview (implemented prototype)

The optional Windows D3D11 sample now loads a serialized Scene v6 fixture and model, resolves the Scene `MeshRenderer` through AssetRegistry and the bounded importer, uploads the selected primitive, and renders it through BGFX. A CTest wrapper validates the reported selection and framebuffer pixel coverage using a temporary project copy. The existing procedural cube smoke remains intact, and an importer-off build was checked. This is a standalone sample/smoke only: Editor Scene/Game View, gameplay runtime, Scene Transform/hierarchy, multi-object drawing and production shader/material support are not included. See [TESTING.md](TESTING.md#071e-standalone-scene-mesh-bgfx-smoke), [sample instructions](../samples/3d_prototype/README.md), and [report](reports/07.1e-scene-bgfx-preview.md).

## 07.1f — Editor Scene View MeshRenderer preview (implemented prototype)

Optional importer-enabled Editor submits visible Scene `MeshRenderer` components through a separate BGFX 3D pass composed into Scene View. GPU vertex/index buffers are cached per entity/reference, and parent/local transforms are composed before submission. 07.1g adds a backend-neutral orbit camera: right-drag orbits, middle-drag pans, wheel zooms, and Frame All fits transformed bounds of visible mesh geometry. 07.1h expands the isolated smoke to two valid meshes and one missing AssetId: it checks continued rendering, combined center `x=4`, a missing-reference diagnostic, and a coarse white-pixel threshold within the normalized Scene View crop of the swapchain screenshot (**11,900** pixels in the measured run). 07.1i adds `astro_editor.exe --benchmark-3d` with 24 warm-up and 48 measured frames for each of 1/100/500/1000 deterministic mesh scenes. Across three Windows D3D11 runs, the 1000-mesh Scene View CPU pass measured **1.42–1.60 ms p50** (mesh prepare plus submission); complete editor-frame wall time was **16.65–16.68 ms p50** under VSync. Full Release CTest passed **51/51 in 15.60 s**. This establishes a measurement baseline only; it does not set budgets or prove uncapped throughput. BGFX GPU timer includes the complete window frame/VSync, not just the 3D pass. Limitations: static positions/colors, explicit Reload after source/derived-cache changes, no normals/materials/light/picking or Game View/runtime integration; Windows D3D11 only. See [07.1i report](reports/07.1i-editor-3d-frame-profile.md), [TESTING.md](TESTING.md#071i-editor-3d-frame-profile), [3D rendering notes](3D_RENDERING.md#editor-3d-frame-profile-071i), [D132](DECISIONS.md#d132--editor-3d-frame-profile-scope-071i). Next recommended substage: 07.2 material authoring/PBR capability assessment and contract; the measurements show frame-time headroom in this test but no quality or GPU budget has been approved.

## 06.28 — Язык интерфейса Editor

Реализован переключаемый English/Russian интерфейс Windows Editor с пользовательским сохранением настройки и тестом покрытия UI-строк. Перевод runtime/SDL demo, низкоуровневых и динамических диагностик не входит в эту ограниченную поставку. Детали и остаточные ограничения: [LOCALIZATION.md](LOCALIZATION.md), [отчёт 06.28](reports/06.28-editor-russian-localization.md).

## 06.29 — Локализация SDL Game UI и меток диагностики

SDL Game UI demo предоставляет выбор English/Russian на экране настроек. Переводятся оболочка меню/HUD, встроенные названия настроек и известные ошибки GameUiModel/адаптера. Неизвестное сообщение сохраняется как техническая деталь с русской меткой; текст SDL/драйвера остаётся источником на языке backend. См. [отчёт 06.29](reports/06.29-runtime-and-diagnostics-localization.md).

## 06.30 — Сохранение языка runtime и диагностики предпочтений

Runtime Game UI сохраняет выбранный `en|ru` в versioned user preference под SDL user-data root; project и game saves остаются независимыми. Чтение, round-trip, malformed preference и реальная настройка через UI проверяются unit/smoke тестами на Unicode-пути и test-owned temporary folder. Динамические ошибки записи получают точный русский префикс с сохранением исходной системной детали. Детали и результаты: [отчёт 06.30](reports/06.30-runtime-language-persistence.md).

## 06.31 — Ручные сохранения кампании в Editor Game View

Editor Game View теперь использует существующий SaveStore/PuzzleCampaignSaveAdapter для слотов 0–9. В профиль явно включены только Puzzle snapshot, ID кампании/уровня и END; user-data путь наследует project settings и добавляет `PuzzleCampaigns/<campaign-id>`. Load сначала валидирует слот и текущие авторские правила, затем заменяет активную campaign session; ошибка не меняет игру. Слоты ручные: autosave, migrations и previews отсутствуют. Smoke также проверяет пустой слот и project/user-data separation. Результаты: [отчёт 06.31](reports/06.31-editor-campaign-progress.md).

## 06.32 — Ручная проверка UI сохранений кампании

Живая проверка выявила, что исходный first-use размер Game View обрезал кнопки Save/Load и не оставлял достаточно места для поля доски. Для обычного интерактивного Editor задан начальный размер 520×560, кнопки Save/Load расположены отдельной строкой; smoke/benchmark сохраняют прежнюю геометрию для стабильности графической регрессии. В настоящем Release Editor на изолированной копии sample-проекта сохранён слот 0, легальный ход изменил счёт 0→180, а Load восстановил `intro`, исходную доску и счёт 0. Файл появился только под SDL user data. Следующий шаг с миграцией ID кампаний не выполнялся. Точные результаты: [отчёт 06.32](reports/06.32-editor-campaign-save-ui.md).

## 06.33 — Identity и границы переноса campaign slots

Каталог сохранений вычисляется централизованно и проверяемо: SDL user-data root + project settings subdirectory + `PuzzleCampaigns/<campaign-id>`. Изменение ID кампании выбирает новый каталог и не перемещает/удаляет старый. Перенос или копия проекта с `.astro/puzzle-save.settings` сохраняет тот же относительный namespace; поэтому такие копии используют общие слоты, пока пользователь не задаст отдельный каталог в настройках. Проект без sidecar использует path-hash default текущего корня. Автоматической миграции нет. Smoke проверяет Unicode, копию sidecar, разделение campaign ID и отказ небезопасного ID, не создавая игровые слоты и не обращаясь к user AppData. [Отчёт 06.33](reports/06.33-campaign-save-identity.md).

Источник: раздел 10 требований пользователя. Подэтапы 00.1–00.3, 01.1–01.5, 02.1–02.4, 03.1–03.5, 04.1–04.4, 05.1–05.10 и 06.1–06.36 реализованы в ограниченном объёме по отчётам. Остальные этапы запланированы; наличие документов само по себе не означает прохождение приёмки.

## 06.34 — Оценка безопасного переноса campaign slots (design only)

Проверены реальные контракты namespace и загрузчика: копия sidecar сохраняет общий каталог, а save payload проверяет точный campaign ID. Поэтому перенос между разными campaign ID не совместим с текущим загрузчиком и не должен выполняться как файловое копирование. Для будущего переноса одинакового ID сначала требуется read-only preview по ограниченным слотам 0–9, включая целостность/backup состояние и конфликты назначения; действующая папка-источник должна оставаться нетронутой. Смена namespace, копирование файлов и преобразование payload не реализованы. Для любых записей потребуется отдельное подтверждение непосредственно перед операцией. [Отчёт 06.34](reports/06.34-campaign-save-transfer-assessment.md).

Источник: решение D096 и отчёт 06.34. Подэтап выполнен как анализ без кода и без доступа к сохранениям пользователя.

## 06.35 — Read-only preview сохранений кампании (implemented, API only)

Добавлены read-only открытие SaveStore и `previewPuzzleCampaignSaveTransfer`: без создания каталогов preview проверяет ровно слоты 0–9, целостность и fallback на `.bak`, ID кампании, обязательные поля, размеры ожидаемых файлов, конфликты primary/backup на цели и неожиданные исходные артефакты. Проверки пути отклоняют `..`, symlink и Windows reparse points. Integration fixture покрывает Unicode path, пустые/валидные/повреждённые/восстановленные слоты, чужой ID, конфликты, отсутствие записи и неизменность source bytes. Перенос файлов, согласие на запись и валидация сохранённого snapshot против текущих правил кампании не входят. [Отчёт 06.35](reports/06.35-campaign-save-transfer-preview.md).

## 06.36 — Editor UI для read-only preview переноса сохранений (implemented)

В `Puzzle Save Settings` добавлено **Preview save transfer**. Источник берётся из текущей настройки проекта, цель — из редактируемого поля каталога. UI показывает результаты 10 слотов, размер, совпадение ID, резервные копии, конфликты и неожиданные артефакты, используя только API 06.35. Preview очищается при правке целевого пути. Получение Windows data root читает `%APPDATA%` и не вызывает `SDL_GetPrefPath`, чтобы интерфейс не создавал каталог. Smoke использует временный каталог, проверяет отображение preview и неизменность отсутствующей цели и исходного файла. Копирования, сохранения настройки, миграции и разрешения конфликтов нет. Preview не гарантирует совместимость snapshot с текущими правилами уровней. Полный Release CTest — **52/52**, 78,53 с; см. [06.36 report](reports/06.36-campaign-save-transfer-editor-preview.md) и [D098](DECISIONS.md#d098--show-campaign-save-transfer-preview-in-editor-read-only-0636).

## 06.37 — Revalidation contract для переноса campaign saves (implemented, no-write)

Добавлен `revalidatePuzzleCampaignSaveTransferPreview`: непосредственно перед будущим переносом он повторно получает preview и возвращает `Ready`, `StaleSource`, `StaleDestination`, `DestinationConflict` или `SourceNotTransferable`. Для обнаружения изменений содержимого первичных и `.bak` файлов preview хранит ограниченный FNV-1a отпечаток (файлы больше лимита формата не допускаются к preflight). Это только обнаружение случайного устаревания, не криптографическая защита. Любая существующая запись по ожидаемому target slot path считается конфликтом, включая пустой файл или каталог. Невалидные/чужие/слишком большие слоты и неожиданные source artifacts блокируют `Ready`; пустые слоты разрешены, если есть хотя бы одно допустимое сохранение. Никакой файл не копируется/заменяется, не создаются каталоги; каждый будущий перенос по-прежнему требует нового просмотра и явного подтверждения. Release full CTest **52/52**, 54,46 с; см. [06.37 report](reports/06.37-campaign-save-transfer-preflight.md) и [D099](DECISIONS.md#d099--revalidate-campaign-save-transfer-preview-before-write-0637).

## 06.38 — Copy-only transaction для campaign saves (runtime API, без UI)

`copyPuzzleCampaignSaveTransfer` принимает только подтверждённый вызывающей стороной запрос и повторно выполняет 06.37 preflight до создания target-папки. Копируются только primary/`.bak` артефакты распознанных same-ID слотов, побайтно. Создание цели эксклюзивное (`CREATE_NEW` на Windows, `O_EXCL` на POSIX); существующие файлы не перезаписываются. При отмене/ошибке удаляются лишь файлы, созданные этой попыткой и всё ещё совпадающие с её выходом, а пустые созданные папки удаляются в обратном порядке. Конкурирующий файл сохраняется; невыполненный rollback возвращает список артефактов. Это rollback при обнаруженной ошибке, не crash-atomic транзакция: аварийное завершение может оставить частичный набор. Прогресс callback может отменить операцию. UI-кнопки и реальные каталоги пользователя не подключены. Тесты копируют только изолированные temp fixtures. Release CTest **52/52**, 59,88 с. См. [06.38 report](reports/06.38-campaign-save-copy-transaction.md) и [D100](DECISIONS.md#d100--same-id-copy-only-campaign-save-transaction-0638).

## 06.39 — Подтверждённое копирование сохранений кампании в Editor

`Puzzle Save Settings` связывает UI с copy API 06.38. После read-only preview Editor предлагает отдельный диалог подтверждения с точными source/target путями, числом валидных слотов, объёмом данных и предупреждением о создании файлов. Кнопка подтверждения вызывает повторную проверку и копирование; существующие файлы не заменяются. Результат показывает число файлов/байт, диагностику и оставшиеся пути при неполном rollback. Smoke проводит тот же путь подтверждения на собственной временной Unicode-фикстуре и сверяет байты; реальные пользовательские сохранения не затрагиваются проверкой. Миграции нет, аварийное завершение всё ещё может оставить частичную цель. См. [06.39 report](reports/06.39-campaign-save-transfer-editor-confirmation.md) и [D101](DECISIONS.md#d101--editor-confirmation-before-campaign-save-copy-0639).

## 06.40 — Журнал и диагностика прерванного переноса

Перед записью слотов runtime эксклюзивно создаёт ограниченный `ASTRO_CAMPAIGN_TRANSFER 1` journal с ID кампании и ожидаемыми именами/размерами/отпечатками файлов; запись сбрасывается на диск до копирования. Успешная операция и штатный rollback удаляют только созданный этой операцией неизменённый journal. Если процесс прекращён между файлами, следующий preview обнаруживает journal, классифицирует каждый target artifact как полный, частичный, отсутствующий, изменённый или небезопасный; новая копия блокируется. Editor показывает journal/artifact пути и требует отдельного подтверждения, чтобы удалить только запись журнала. Save-файлы этим действием не меняются; частичные/изменённые файлы пользователь проверяет вручную. Integration test действительно завершает дочерний процесс после первого файла на Unicode temp fixture и проверяет состояние после повторного запуска тестового процесса. Это обнаружение process interruption, не атомарный commit и не гарантия устойчивости при отключении питания. См. [06.40 report](reports/06.40-campaign-save-transfer-recovery-journal.md) и [D102](DECISIONS.md#d102--journal-and-report-campaign-save-transfer-interruption-0640).

## 06.41 — Явное продолжение прерванного переноса

Добавлен отдельный `resumePuzzleCampaignSaveTransfer` и подтверждающий диалог Editor. Runtime сверяет неизменность журнала, кампанию, состав/содержимое source primary и `.bak` файлов с записью журнала и повторно классифицирует цель непосредственно перед записью. Полные target-файлы пропускаются; отсутствующие создаются только эксклюзивно. Изменённый источник возвращает `StaleSource`; изменившаяся после просмотра цель требует повторной диагностики; Partial/Changed/Unsafe блокируются без записи. При сбое/отмене откатываются только файлы, созданные текущим продолжением, а журнал остаётся для следующего просмотра. Integration test прерывает дочерний процесс, проверяет отказ без подтверждения, успешное продолжение, изменённую цель и устаревший источник. Editor smoke подтверждает отображение действия продолжения. Это не решает ручную очистку частичных файлов, повреждённый журнал, конфликт двух одновременных процессов или устойчивость к отключению питания. См. [06.41 report](reports/06.41-campaign-save-transfer-resume.md) и [D103](DECISIONS.md#d103--explicit-resume-for-campaign-save-transfers-0641).

## 06.42 — Политика частичных файлов переноса

Частичная запись target остаётся конфликтом: runtime resume отказывает с `DestinationConflict`, не удаляет её, не меняет содержимое и оставляет журнал. Editor показывает отдельное предупреждение и отключает resume, пока среди артефактов есть Partial. Это намеренно: журнал сообщает ожидаемый полный fingerprint, но не хранит fingerprint частичного префикса и не аутентифицирует происхождение target-файла. Совпадение байтов с началом source не доказывает, что файл создала именно эта операция; поэтому append/delete/replace-авторизация не добавляется. Integration test проверяет префиксный partial target после имитации process interruption и подтверждает неизменность его байтов, отсутствующей пары и журнала; Editor smoke проверяет предупреждение и заблокированное состояние. Дальнейшая ручная очистка — только после пользовательского просмотра вне автоматического resume. См. [06.42 report](reports/06.42-partial-campaign-save-transfer-policy.md) и [D104](DECISIONS.md#d104--preserve-partial-campaign-save-targets-0642).

## 06.43 — Диагностика повреждённого журнала переноса

`PuzzleCampaignSaveTransferRecovery` сообщает отдельную причину непригодности журнала: `Unreadable`, `Unsafe`, `Oversized` или `Malformed`. Неизвестная/усечённая структура, будущая версия, небезопасный тип пути и превышение лимита блокируют копирование, resume и acknowledgement. Для невалидного журнала runtime не угадывает slot-имена или artifact manifest; Editor показывает категорию и исходную диагностику, сообщает, что файлы не менялись, и просит проверить журнал/каталог вручную. Integration test покрывает будущую версию, усечение, превышение лимита и каталог на месте journal; проверяет неизменность candidate save и отказ всех операций. Editor smoke аналогично сохраняет malformed journal и похожий на slot файл. Это не восстановление/очистка журнала и не проверка происхождения файлов. См. [06.43 report](reports/06.43-invalid-campaign-transfer-journal-diagnostics.md) и [D105](DECISIONS.md#d105--diagnose-invalid-campaign-transfer-journals-0643).

## 06.44 — Подтверждённый архив читаемого malformed-журнала (поведение уточнено в 06.45)

В 06.44 появился путь подтверждённого архивирования; проверка 06.45 убрала небезопасное удаление оригинала. Текущее поведение описано в [06.45](reports/06.45-race-safe-malformed-journal-archive.md) и [D107](DECISIONS.md#d107--preserve-malformed-journals-during-archive-0645).

## 06.45 — Архивная копия без гонки удаления malformed-журнала (implemented)

Для читаемого ограниченного `Malformed` журнала Editor показывает отдельное подтверждение создания архивной копии. Runtime эксклюзивно создаёт соседний файл с точными байтами и проверяет его; оригинал намеренно остаётся на месте и продолжает блокировать переносы до ручной проверки. Повторная операция принимает только идентичную архивную копию; коллизия с другими байтами не перезаписывается. В 06.48 добавлена lifecycle-блокировка исходного журнала на время архивирования и проверены отказы при конкурирующем Windows/POSIX lock. Блокировка не меняет политику сохранения исходника: удаление malformed-журнала не реализовано. Слоты сохранений не читаются и не меняются. См. [06.45 report](reports/06.45-race-safe-malformed-journal-archive.md), [06.48 report](reports/06.48-malformed-journal-archive-lock.md) и [D111](DECISIONS.md#d111--lock-malformed-journal-archive-0648).

## 06.46 — Безопасное подтверждение корректного recovery-журнала (implemented with platform limits)

Windows открывает проверенный журнал через `CreateFileW` с `GENERIC_READ | DELETE` и share mode только для чтения, проверяет точные байты через этот handle и удаляет тот же file object через `SetFileInformationByHandle`. Если конкурирующий writer или filesystem мешает получить handle/удалить, операция отказывает и оставляет запись. На POSIX API пока fail-closed; будущий backend требует `O_NOFOLLOW`, advisory lock, проверки inode через открытый directory fd и `unlinkat`. Advisory lock действует только для сотрудничающих процессов, поэтому POSIX-поддержка остаётся выключенной до отдельной платформенной сборки и race-тестов. Integration тесты Windows проверяют устаревший журнал, удерживаемый writer handle, успешное подтверждение и сохранность слотов. См. [06.46 report](reports/06.46-handle-based-campaign-journal-acknowledgement.md) и [D108](DECISIONS.md#d108--handle-based-valid-recovery-journal-acknowledgement-0646).

## 06.47 — POSIX acknowledgement и блокировка журнала на время транзакций (implemented)

Подтверждение recovery-журнала теперь работает на Windows и POSIX. Copy и resume удерживают lease одного и того же журнала на всём протяжении транзакции; acknowledgement сначала сверяет показанный preview и список артефактов, затем берёт ту же блокировку, повторно сравнивает точные байты и удаляет только journal entry/file object. Windows использует `LockFileEx` плюс удаление открытого объекта через `SetFileInformationByHandle`; POSIX открывает каталог и файл без следования symlink, берёт `flock`, сверяет `fstat`/`fstatat(AT_SYMLINK_NOFOLLOW)` и вызывает `unlinkat`. POSIX advisory lock координирует только процессы, соблюдающие протокол; произвольный локальный процесс может его игнорировать. Запись игровых слотов acknowledgement не изменяет. Проверены отдельный блокирующий процесс на POSIX, конкурирующая блокировка Windows и отказ во время активного copy. Ubuntu 24.04 под WSL2 использует сборку в Linux filesystem. Финальные проверки: Windows Release CTest 52/52; POSIX Release CTest 39/39. См. [06.47 report](reports/06.47-posix-journal-acknowledgement-readiness.md), [D110](DECISIONS.md#d110--cross-platform-campaign-journal-lifecycle-lock-0647) и историческое обоснование readiness в [D109](DECISIONS.md#d109--gate-posix-journal-acknowledgement-on-a-runnable-target-0647-assessment).

## 06.48 — Lifecycle lock для архивирования malformed-журнала (implemented)

Архивирование сначала валидирует displayed preview, затем получает lease исходного bounded malformed-журнала, сравнивает его точные байты через этот handle и держит lease до верификации соседней копии. Если transfer/archive lock уже занят, операция отказывает до создания архива. Оригинал намеренно сохраняется; слоты не затрагиваются. Тест покрывает конкурентный `LockFileEx` на Windows и удержание POSIX `flock` отдельным процессом, после освобождения операция проходит. Полный Release CTest: Windows 52/52, WSL Ubuntu 24.04 39/39. POSIX advisory-lock ограничение остаётся. См. [06.48 report](reports/06.48-malformed-journal-archive-lock.md) и [D111](DECISIONS.md#d111--lock-malformed-journal-archive-0648).

## 06.49 — Оценка восстановления частичной архивной копии (assessment, superseded by 06.50)

Архив создавался эксклюзивно сразу под итоговым детерминированным именем. Анализ и fixture-тест обнаружили риск частичного финального файла; это поведение исправлено в 06.50. Историческая оценка и воспроизводимый сценарий: [06.49 report](reports/06.49-partial-journal-archive-assessment.md), [D112](DECISIONS.md#d112--preserve-partial-journal-archive-residue-0649).

## 06.50 — Staged atomic no-replace archive publication (implemented)

Malformed-журнал по-прежнему сохраняется и удерживается под прежним lifecycle lease. Архив полностью записывается и flush-ится под уникальным именем рядом с итоговым файлом, повторно сверяется через удерживаемый handle и публикуется без замены: Windows использует handle-based `FileRenameInfo` с `ReplaceIfExists=FALSE`, POSIX — `linkat` и последующее удаление staging entry. Stale staging-файлы после аварийного завершения сохраняются для ручной проверки, но не блокируют новую попытку. Существующий неполный финальный архив всё ещё вызывает отказ, его нельзя автоматически исправить. Integration-тест проверяет сохранность stale staging occupant и конфликт финального пути; это не реальный crash/power-loss тест. Результаты: [06.50 report](reports/06.50-staged-journal-archive.md), [D113](DECISIONS.md#d113--staged-atomic-no-replace-journal-archive-publication-0650).

## 06.51 — Проверка прерывания staged-архива (implemented)

Integration-тест запускает дочерний тестовый процесс и принудительно завершает его после частичной записи, после exact-byte проверки до публикации и после публикации до очистки staging. Для каждого состояния проверяются итоговое имя, остаток staging, последующий retry, неизменность malformed-журнала и контрольного slot-файла. Seam доступен только через внутренний тестовый заголовок и не добавляет управляющую переменную или параметр в production API. Windows Release: focused 1/1, full CTest 40/40; WSL Ubuntu Release: focused 1/1, full CTest 39/39. Это проверка завершения процесса, не питания/ядра/файловой системы. Подробности: [06.51 report](reports/06.51-archive-interruption-tests.md), [D114](DECISIONS.md#d114--subprocess-interruption-tests-for-staged-journal-archive-0651).

## 06.52 — Оценка ручного разбора конфликтного архива (assessment)

Архив с точными байтами проверенного журнала можно безопасно повторно использовать. Отличающийся файл по ожидаемому пути нельзя надёжно признать остатком старой попытки: он может быть прежним частичным архивом 06.49, пользовательским файлом, изменённым архивом или коллизией 64-битного fingerprint. Совпадение префикса также не подтверждает происхождение. Текущий fail-closed отказ сохраняется. Рекомендация для отдельной реализации: read-only диагностика с точным путём, типом, размером, доступностью и результатом сравнения (точный/отличающийся/префикс/недоступный), без автоматического удаления, перемещения или замены. Пользователь вручную разбирает неизвестный объект и повторяет операцию. Код и поведение продукта не менялись; полный baseline CTest до оценки: Windows 40/40, WSL Ubuntu 39/39. См. [06.52 report](reports/06.52-legacy-archive-conflict-assessment.md) и [D115](DECISIONS.md#d115--manual-review-of-legacy-conflicting-archive-files-0652).

## 06.53 — Read-only диагностика конфликта архива (implemented)

Runtime предоставляет инспекцию детерминированного архивного пути только для показанного пользователю, читаемого malformed-журнала. Она классифицирует отсутствующий путь, обычный файл, каталог, ссылку/reparse point, другой тип или недоступный объект. Содержимое читается только у обычного файла и ограничено 4096 байтами; результат — точное совпадение, отличие, префикс, превышение лимита или недоступность. Проверка работает с no-follow handle и сверяет стабильность объекта; исходный журнал дополнительно удерживается под lease. Editor показывает путь, тип, размер, сравнение и диагностику после отказа архивирования. Smoke и integration-тесты проверяют exact/different/prefix/oversized, каталог и ссылку, сохранение байтов журнала, слота и владельца архива. Windows Release полный CTest 44/44; WSL Ubuntu Release 39/39. Это сравнение байтов, не подтверждение владельца; файл не исправляется и не заменяется. См. [06.53 report](reports/06.53-archive-conflict-diagnostics.md), [D116](DECISIONS.md#d116--read-only-diagnostics-for-archive-conflicts-0653).

## 06.54 — Оценка явного сохранения конфликтующего файла копией (assessment)

Рассмотрен только будущий пользовательский workflow. Возможный кандидат: отдельное подтверждение копии читаемого обычного файла размером до 4096 байт в новую соседнюю цель с уникальным именем и публикацией без замены. Источник удерживается и повторно проверяется во время bounded exact-byte копирования; оригинал сохраняется. Ссылка/reparse point, каталог, другой тип, недоступный или oversized объект не копируется. Диалог должен показывать источник, предложенное назначение, размер и сообщение, что авторство неизвестно. Успешная копия не проверена как архив и не очищает исходный конфликт; ручная проверка остаётся обязательной. 06.54 изменил только документы; никакой код, формат, API или данные не менялись. Перед правками Windows Release Editor build завершился (154/154 задач, без ошибок компиляции; были предупреждения C4189), WSL Ubuntu build сообщил `ninja: no work to do`. CTest до документальных изменений: Windows 44/44 (14.95 s), WSL Ubuntu 39/39 (0.80 s). См. [06.54 report](reports/06.54-conflict-copy-assessment.md) и [D117](DECISIONS.md#d117--bounded-explicit-copy-of-unknown-archive-occupants-0654-assessment). Следующий шаг — реализация только после отдельного подтверждения.

## 06.55 — Подтверждаемое сохранение копии конфликтующего файла (implemented)

Runtime preview разрешает только отличающийся bounded regular file до 4096 байт и выбирает свободное соседнее имя. Подтверждение удерживает no-follow source handle, повторно проверяет identity/bytes и malformed journal, пишет уникальный staging-файл и публикует его атомарно без замены. Editor показывает оба пути, размер и предупреждение, что файл не подтверждён и его автор неизвестен; cancel не пишет файлы. Источник, журнал и slot не меняются, копия не очищает исходный конфликт. Тесты покрывают отказ без подтверждения, destination race, stale source, oversized/directory/symlink и процессное завершение до/после публикации. Полный Release CTest: Windows 44/44, WSL Ubuntu 39/39. Ограничения: копия не валидируется как формат журнала; interruption/power loss и hostile local writers за пределами проверенных сценариев; UI диалог проверен Editor smoke, не ручным кликом. См. [06.55 report](reports/06.55-conflict-copy.md), [D118](DECISIONS.md#d118--explicit-bounded-copy-aside-for-archive-conflicts-0655).

## 06.56 — Unicode-путь и ручная инспекция диалога (implemented)

Integration добавляет Unicode-каталог `Проверка копии Ω` и проверяет точные байты в созданной копии, исходном конфликте, журнале и save slot на Windows и WSL/POSIX. Добавлен тестовый режим `--smoke-inspect-copy-dialog`: он открывает встроенный подтверждающий диалог на временной smoke-фикстуре, ждёт ручного действия и при отмене проверяет отсутствие копии и сохранность трёх исходных файлов. Диалог визуально проверен; отмена завершилась кодом 0. Windows Release полный CTest 44/44; WSL Ubuntu Release 39/39. Это проверка двух текущих файловых систем; другие POSIX/macOS/mobile платформы не проверялись. Никакой пользовательский save data не открывался. Подробности: [06.56 report](reports/06.56-unicode-copy-and-dialog-review.md), [D119](DECISIONS.md#d119--unicode-path-and-manual-dialog-verification-0656).

## 06.57 — Оценка переносимости copy-aside на macOS/POSIX (assessment)

Проверены исходный POSIX путь и документация Apple. Используемые descriptor-relative вызовы и no-follow флаги документированы Apple; advisory `flock` годится только для сотрудничающих процессов, а same-filesystem hard link поддерживает атомарное создание новой записи без замены. Критический незакрытый вопрос — durability: обычный macOS `fsync` не гарантирует физическую запись до потери питания, `F_FULLFSYNC` — специфичное Apple расширение, а поведение каталожного `fsync` и фактические гарантии APFS здесь не испытаны. Linux WSL не подтверждает macOS. Код/API не менялись. Windows Release build и полный CTest 44/44, WSL Ubuntu Release build без работы и полный CTest 39/39. См. [06.57 report](reports/06.57-posix-macos-portability-assessment.md), [D120](DECISIONS.md#d120--macos-posix-copy-aside-portability-assessment-0657). Следующий рекомендуемый шаг: macOS CI/host validation этого POSIX transaction на APFS с тестами publication, directory flush outcomes, Unicode, lock contention и interruption; не объявлять durable power-loss safety.

## 06.58 — исправление переносимости тестовой сборки по результату Mac

Пользовательский Mac-прогон выявил неоднозначный `std::to_string` в `CSharpCodeWorkspaceTests.cpp` и `CSharpBuildManifestTests.cpp`. В обоих тестах счётчик `file_time_type` теперь явно приводится к `long long` перед форматированием. Локально Release build и тесты прошли на Windows (focused 2/2, full 44/44) и WSL Ubuntu (focused 2/2, full 39/39). Требуется повторный build/CTest на Mac: локальные результаты не подтверждают macOS. Подэтап ограничен тестовой переносимостью; файловый durability/API путь не менялся. См. [06.58 report](reports/06.58-macos-test-portability-fix.md), [D121](DECISIONS.md#d121--explicit-time-count-conversion-in-cross-platform-tests-0658).

00.1–00.3, 01.1–01.5, 02.1–02.4, 03.1–03.5, 04.1–04.4, 05.1–05.10 и 06.1–06.4: см. отчёты подэтапов.

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
- **04.5** Технический C# scripting prototype: Windows x64 native `hostfxr` host загружает managed component; fixed-step integration проверяет изменение runtime Scene copy и изоляцию authoring Scene при Stop. Проверено с локально распакованным .NET 10 SDK 10.0.404. Host остаётся experimental/optional и не является финальным выбором; Editor, полноценные bindings, sandbox, hot reload, mobile strategy и shipping/package tests остаются вне подэтапа. См. [отчёт 04.5](reports/04.5-csharp-runtime-prototype.md).
- **04.6** Реализован дополнительный технический шаг: versioned C# ABI v1 для fixed-step и ограниченных Transform callbacks. Это добавление к мастер-плану (исходный раздел 10 не содержал отдельного 04.6); см. [CSHARP_SCRIPTING_API.md](CSHARP_SCRIPTING_API.md) и [отчёт 04.6](reports/04.6-versioned-csharp-runtime-api.md).
- **04.7** Реализован дополнительный технический шаг: persistent `ScriptBehavior` binding в Astro Scene v5 с AssetId, managed type name, enabled/schema fields, чтением v1–v4 и round-trip v5. Lifecycle описан как контракт; runtime dispatch, asset resolver и Editor authoring не входят в этот шаг. Это добавление к мастер-плану; см. [отчёт 04.7](reports/04.7-persistent-script-behavior.md).
- **04.8** Реализован дополнительный технический шаг: общий Asset index v2 с глобально уникальными AssetId для image и C# scripts; сохранение источника, type, assembly/runtimeconfig paths; проверяемое разрешение ID через optional Windows hostfxr adapter. Registry не компилирует код и не исполняет lifecycle; Editor authoring и sandbox не добавлены. См. [C# scripting API](CSHARP_SCRIPTING_API.md) и [отчёт 04.8](reports/04.8-project-script-asset-registry.md).
- **04.9** Реализован дополнительный технический шаг: локальный C# build manifest и CLI с точным pin .NET SDK, offline restore, детерминированной компиляцией, immutable output generations, проверкой source/build/output hashes и stale diagnostics. Только `Scripts/**/*.cs`; CLI не связывает компонент с RuntimeSession, не исполняет код и не является sandbox. Это добавление к мастер-плану; см. [C# scripting API](CSHARP_SCRIPTING_API.md), [FILE_FORMATS](FILE_FORMATS.md) и [отчёт 04.9](reports/04.9-csharp-project-build.md).
- **04.10** Реализован дополнительный шаг: `scriptBindings` в build manifest, versioned C++ handoff, AssetRegistry freshness/status и явная атомарная синхронизация Script AssetIds; Project panel показывает сборочные статусы/диагностики и действие регистрации. Наличие managed type не проверяется до явного host load; автоматический lifecycle и Code workspace не добавлены. См. [отчёт 04.10](reports/04.10-csharp-build-registry-diagnostics.md).
- **04.11** Определена политика разрешения на будущий Play-запуск C#: один интерактивный opt-in на точный набор ScriptBehavior и build generation, без сохранения доверия и без AI auto-approval. Это проектное решение, не исполняемый gate; UI/runtime интеграция остаются отдельным шагом. См. D077 и [отчёт 04.11](reports/04.11-csharp-play-approval-policy.md).
- **04.12** Завершён Editor consent gate для точного набора C# bindings/build hashes, с повторной проверкой на подтверждении и односессионным разрешением.
- **04.13** Завершено подключение enabled ScriptBehavior к RuntimeSession lifecycle с повторной проверкой consent перед каждой уникальной host-загрузкой; добавлены тесты и документация ABI.
- **04.14** Завершён Inspector authoring зарегистрированных C# bindings с транзакционными add/enable/remove, сохранением Scene v5 и Undo/Redo.
- **04.15** Добавлена базовая C# Code workspace: открытие/создание/сохранение UTF-8 `.cs` только внутри `Scripts`, unsaved-change prompt, явный запуск существующей сборки, compiler diagnostics с переходом к файлу/строке. Это ограниченный Windows Editor flow без syntax highlighting, LSP, debugger или sandbox. См. [отчёт 04.15](reports/04.15-csharp-code-workspace.md).
- **04.16** Добавлена форма project build settings для `.astro/csharp-build.json`: SDK pin, фиксированный `net10.0`, assembly name, source/type bindings. Строгие schema/path checks и атомарная запись; повреждённый манифест требует явного подтверждения замены. Build pipeline читает этот манифест. См. [отчёт 04.16](reports/04.16-csharp-build-manifest-editor.md).
- **04.17** Project C# report различает отсутствующую регистрацию, расхождение managed type, source hash и assembly outputs. Несовпадение исправляется явной синхронизацией текущего build; Script AssetId сохраняется. Это проверка manifest/build/registry, не CLR reflection: существование типа подтверждается только при загрузке в одобренной Play-сессии. См. [отчёт 04.17](reports/04.17-csharp-binding-diagnostics.md).

Приёмка: интерактивная сцена управляется и не портит исходную сцену.

## ЭТАП 05 — ПЕРВАЯ ЗАКОНЧЕННАЯ 2D-ИГРА

- **05.1** Минимальная 2D-физика/коллизии и события.
- **05.2** Game UI: меню, HUD, пауза, настройки.
- **05.3** Звук, анимации, игровая логика головоломки.
- **05.4** Save System v1: выбор сохраняемых полей, слоты, load/save,
     версионирование, защита от повреждённой записи.
- **05.5** Checkpoint v1: триггер и респаун, выбранные группы данных.
- **05.6** Windows runtime packaging; smoke-тест чистого запуска.
- **05.7** Подключение визуальной Puzzle-доски и ходов к Gameplay-экрану runtime demo.
- **05.8** Загрузка авторской Puzzle-кампании, fixed-step ходы, переходы и terminal END в standalone runtime demo.
- **05.9** Явное ручное сохранение/восстановление выбранного прогресса авторской кампании в standalone runtime demo.
- **05.10** Самодостаточный Windows runtime ZIP с примером кампании, относительным launcher и smoke сохранения/восстановления из распаковки.

Приёмка: 2D-головоломка с несколькими уровнями, меню, звуком,
сохранением прогресса и самостоятельным Windows-приложением. Эта приёмка
пока не выполнена: 05.9 добавляет ручное сохранение campaign progress, а 05.10
включает один пример кампании в ZIP, но это не завершает весь игровой цикл и
экспорт произвольного проекта. 06.31 добавляет ручные campaign slots в Editor
Game View; автосохранение и миграции отсутствуют. Новый ZIP проверен из чистой распаковки;
это обновляет demo-артефакт, но не завершает самостоятельный экспорт игры.

## ЭТАП 06 — NO-CODE И РЕДАКТОРЫ

Подэтап 06.1 реализован ограниченно: typed pure-dataflow prototype и базовая панель Visual Logic. Граф пока не сохраняется, не исполняет команды и не меняет Scene; отсутствуют flow-control и drag-wire authoring. Это не приёмка этапа 06.

Подэтап 06.2 реализован ограниченно: in-memory UI document authoring, две встроенные темы, Desktop/Tablet/Phone responsive preview и две валидируемые Scene bindings. Документ не сохраняется и не используется runtime Game UI; полная приёмка этапа 06 не достигнута.

Подэтап 06.3 реализован ограниченно: scalar timeline/keyframe authoring and preview playback, plus CameraRig presets/custom pose panel. Neither timeline values bind to Scene properties nor camera output is rendered; full Stage 06 acceptance remains open.

Подэтап 06.4 реализован ограниченно: in-memory explicit profile fields and entity groups, checkpoint authoring/XY preview, plus an isolated SaveStore/CheckpointSystem restore preview. Project persistence and unsupported gameplay properties remain open; see [SAVE_SYSTEM_EDITOR.md](SAVE_SYSTEM_EDITOR.md) and [report](reports/06.4.md).

Перед шаблонами 06.5 добавлена ограниченная prerequisite-основа: Canvas2D, SDL3 renderer, affine-композиция иерархии и standalone project Scene preview из существующего Scene-файла/AssetRegistry с безопасной проверкой путей. WIC texture cache остаётся Windows-only. Editor Play workflow, level/progress persistence и сами шаблоны не реализованы. См. [RENDERER_2D.md](RENDERER_2D.md) и [отчёт](reports/06.5-hierarchy-project-preview.md).

Для T01 Puzzle добавлены in-memory `PuzzleSnapshot`, явно выбранные custom values в SaveStore v2 и optional adapter `astro_runtime_puzzle_save` с версионированным slot round-trip и транзакционной загрузкой Scene. Подэтап 06.15 добавил встроенный playable preview в Game View с соседними tile swaps и fixed-step runtime smoke. Это всё ещё не T01 project template: authoring уровней и настройка SaveProfile/checkpoint для Puzzle отсутствуют. См. отчёты [SaveStore custom fields](reports/06.6-save-custom-fields.md), [Puzzle save adapter](reports/06.7-puzzle-save-adapter.md) и [Game View](reports/06.15-puzzle-game-view.md).

## T01 Puzzle — ручные слоты в Game View (06.16)

Puzzle preview подключён к `astro_runtime_puzzle_save`: Game View предлагает слоты 0–9 и ручные Save/Load, сохраняя только явно выбранный `puzzle.snapshot`. Обычный Editor использует SDL user-preferences directory; smoke использует удаляемую тестовую папку. Это не профильная система авторинга или шаблон: путь и поле для sample mode пока фиксированы. Результаты: [отчёт 06.16](reports/06.16-puzzle-save-game-view.md).

## T01 Puzzle — project-scoped профиль и каталог прогресса (06.17)

Game View позволяет настраивать SaveProfile ID/schema и user-data relative subdirectory в per-project `.astro/puzzle-save.settings`. Формат `project.astro` v1 не менялся. Новые проекты получают разные default subdirectories; изменённая подпапка и профиль применяются только после `Apply project settings`. Smoke проверяет round-trip, path validation, project isolation и сохранение/загрузку Puzzle state. Старые глобальные слоты не перемещаются; их можно открыть, выбрав прежнюю подпапку вручную. Результаты: [отчёт 06.17](reports/06.17-project-puzzle-save-settings.md).

## T01 Puzzle — авторинг и отдельный файл уровня (06.18)

Game View содержит первую панель Puzzle Level Authoring: редактирование размеров/параметров и стартовых клеток, загрузка/сохранение файла уровня под `Puzzle/Levels`, запуск выбранного draft. Runtime `PuzzleGame` получает отдельный overload `create(PuzzleLevelDefinition)`; старый API генерации по seed сохранён. Format v1 валидирует ID, UTF-8 имя, bounds и tile IDs. Authoring slots отключены, так как SaveStore не связывает payload с level ID. Unit, file integration и Editor smoke проверяют создание/загрузку и запуск уровня. Это первый ограниченный авторский шаг, не полный T01 template, список уровней или прогресс кампании. См. [гайд](PUZZLE_LEVELS.md) и [отчёт 06.18](reports/06.18-puzzle-level-authoring.md).

- **06.1** Visual Scripting, типизированные узлы и отладка.
- **06.2** UI Editor, темы, привязки, responsive layout.
- **06.3** Timeline/анимации, пресеты камер.
- **06.4** Save System Editor: профили, группы, визуальная расстановка
     checkpoint, ручное тестирование восстановления.
- **06.5** Библиотека первых 2D-шаблонов: Puzzle, Platformer, Top-Down.
- **06.6** Расширение C# Code workspace: UX с управляемыми файлами/manifest, улучшенная навигация и диагностика, интеграция Inspector bindings/runtime с Editor smoke; не обещать IDE-сервисы или sandbox без отдельной реализации и оценки.

Приёмка: новая механика собирается в GUI без написания C++.

## ЭТАП 07 — 3D И РЕДАКТОР МИРОВ

- **07.1** 3D meshes, glTF, 3D камеры, 3D физика. 07.1a добавил backend-neutral camera/mesh контракт и опциональный bgfx cube smoke. 07.1b реализовал отдельный opt-in ограниченный glTF/GLB importer/cache для static triangle POSITION геометрии; зависимости локальны и проверяются по commit/hash, скрытая загрузка CMake отключена. Windows Release импортной конфигурации собрался, CTest 42/42; стандартная headless Release конфигурация также собирается без импортёра. Scene/AssetRegistry/Editor integration, материалы/текстуры, нормали/UV, node transforms, анимация и physics остаются отдельными будущими частями. Нужны fuzz/sanitizer и проверки на macOS/mobile до более широких заявлений. См. [07.1 assessment](reports/07.1-3d-foundation-assessment.md), [07.1b report](reports/07.1b-bounded-gltf-importer.md), [D122](DECISIONS.md#d122--keep-3d-prototypes-modular-before-dependency-adoption-071-assessment), [D125](DECISIONS.md#d125--optional-bounded-gltf-static-mesh-importer-071b).
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

- **09.1** Локальный AI Editor API, перечень команд и схемы — реализован базовый in-process C++ фасад 1.0 и каталог зарегистрированных Scene-команд; transport/JSON/permissions не входят в эту поставку. См. [отчёт 09.1](reports/09.1.md).
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

Optional CPack target creates a ZIP of the current SDL3/ImGui Game UI demo with third-party license texts. 05.10 adds the small authored sample campaign under `game-data/puzzle-campaign` and a `%~dp0` relative launcher. The smoke extracts the archive to a fresh unique directory, validates package files, launches the menu and bundled campaign save/restore. Arbitrary project files, Editor and build tools remain outside the ZIP; full game export is still not implemented. Original results: [report 05.6](reports/05.6.md); 05.9 refresh: [report](reports/05.6-package-refresh-after-05.9.md); self-contained sample package: [report 05.10](reports/05.10-self-contained-campaign-package.md); [BUILD_GUIDE](BUILD_GUIDE.md#упаковка-windows-runtime-этап-056).

## Подэтап 05.7 — Puzzle board in runtime Game UI

The optional SDL demo draws an 8×8 seeded Puzzle board via an additive host content callback on the Gameplay screen. Neighbor selection/swap updates the existing score/level HUD; Play resets the board. A deterministic smoke checks a valid move, rendered Gameplay draw data, HUD update, Pause/Resume/menu and isolation of the authoring Scene. It does not automate physical clicks, provide a board screenshot golden, connect campaign/save/checkpoint progress, or verify a rebuilt 05.6 ZIP. Full Stage 05 acceptance remains incomplete. Details and exact checks: [report 05.7](reports/05.7-puzzle-game-ui.md).

## Подэтап 05.8 — Authored Puzzle campaigns in standalone runtime

The SDL Game UI demo accepts a UTF-8 project root and campaign ID, validates project campaign/level files, displays the loaded authored board, and applies tile input on RuntimeSession fixed steps. After the level's target score, an explicit Continue action resolves one transition and loads the next level. `END` retains the final board/score. Added a small read-only two-level project at [samples/ПазлКампания](../samples/ПазлКампания/README.md) and a CTest smoke that runs accepted moves through both levels, follows a transition to END, and checks the result remains available. Does not persist progress or rebuild/package the updated executable. Details: [report 05.8](reports/05.8-runtime-puzzle-campaign.md).

## Подэтап 05.9 — Manual campaign progress persistence

Standalone SDL Game UI provides ten explicit SaveStore slots. The profile selects campaign ID, active level ID, terminal END flag and deterministic Puzzle snapshot; no Scene properties or project files are selected. Project-specific storage is beneath SDL user data. Restore rejects incompatible profile/game versions, campaign/level mismatch and changed authored Puzzle rules before publishing. Smoke and integration tests cover a pending completion and terminal END round-trip. No autosave, migrations, Editor Game View slots or rebuilt package are included. Details: [report 05.9](reports/05.9-campaign-progress-save.md).

## Подэтап 05.10 — Self-contained campaign in Windows runtime ZIP

The optional Windows package now includes the checked-in two-level Puzzle campaign and a command launcher that resolves both executable and campaign data relative to its own location. Fresh-extraction CTest verifies files, menu launch and campaign manual save/restore; a separate manual check verifies the `.cmd` launcher from a different current working directory. This remains an example runtime package, not arbitrary project export. No Editor, build tools, autosave, save migration, or completed Stage 05 gameplay acceptance is implied. See [report 05.10](reports/05.10-self-contained-campaign-package.md).

## 2D Editor preview GPU path

Scene/Game View рендерят SpriteRenderer quads напрямую в bgfx framebuffer. В 06.11 добавлена пиксельная smoke-регрессия нескольких текстур/alpha/resize. В 06.12 устранён квадратичный поиск Transform; оптимизированный 10k Debug CPU p50 — 24–26 ms/вид, Release — 4.7–4.9 ms/вид на D3D11. В Release собраны локальные bgfx библиотеки и полный Editor. Подробности: [отчёт 06.12](reports/06.12-dense-scene-geometry.md), [отчёт 06.11](reports/06.11-bgfx-visual-dense-release.md), решение [D046](DECISIONS.md#d046--linear-scene-transform-composition-for-dense-2d-scenes).
## 06.19 — Puzzle progress slots scoped to authored level (implemented)

The optional Puzzle save adapter now explicitly selects `puzzle.level.id` for authored-level snapshots; load rejects a mismatched ID before publishing candidate state. Editor slots use a separate `PuzzleLevels/<ID>` directory under that project's user-data namespace. The built-in Puzzle sample keeps the existing profile/path. Manual slots only; no campaign model, automatic save policy, or schema migration.
## 06.20 — Authored Puzzle level catalog (implemented)

Add bounded, safe catalog enumeration with diagnostics for malformed entries, selection/open, create-only duplication, and confirmed ID rename. Destination collisions never replace existing levels. Rename retains the existing file format, rechecks the source, publishes the new ID before removing the old path, and does not move player progress slots. No delete UI, campaign graph, or draft undo is included; see the [06.20 report](reports/06.20-puzzle-level-catalog.md).

## 06.21 — Puzzle campaign order and transition format (design)

Defined a versioned UTF-8 project document with a distinct level display order, explicit starting ID, ordered score-threshold branches, and a default level/END result. Contract: [PUZZLE_CAMPAIGNS.md](PUZZLE_CAMPAIGNS.md); design report: [06.21](reports/06.21-puzzle-campaign-format-design.md).

## 06.22 — Strict Puzzle campaign parser and integration tests (implemented)

Implemented the bounded v1 parser, serializer, structural/reference validator, and atomic project-file API. Integration tests reject malformed syntax and invalid references and verify UTF-8 round-trip. Editor authoring, runtime transition resolution, schema migration, and rename protection remain future sub-stages; see [06.22 report](reports/06.22-puzzle-campaign-parser.md).

## 06.23 — Puzzle campaign runtime transition resolver (implemented)

Added a pure one-edge resolver: first matching descending score threshold, otherwise DEFAULT, with END represented by an empty optional. It rejects invalid campaign data and unknown completed-level IDs and never loads or recursively advances a destination. Integration coverage and limits: [06.23 report](reports/06.23-puzzle-campaign-resolver.md).

## 06.24 — Puzzle campaign runtime level loading and END state (implemented)

Added `PuzzleCampaignSession` to load START and subsequent authored levels as playable `PuzzleGame` instances. Level completion is gated on the authored Puzzle target score; the final score selects one resolver edge. Failed next-level load leaves the current completed game intact for retry. END preserves the final level/score in an `Ended` session and rejects more gameplay; creating a new session restarts from START. This does not load engine Scenes or save campaign progress. Tests and exact results: [06.24 report](reports/06.24-puzzle-campaign-session.md).

## 06.25 — Connect Puzzle campaign session to Editor Game View (implemented)

Game View starts a project campaign by ID, routes tile input through the fixed-step runtime callback, exposes explicit level continuation, and displays terminal END while retaining the final board/score. Smoke covers two authored levels, runtime input, transition loading, and END. Manual campaign progress slots were added later in 06.31; standalone SDL campaign saves remain implemented in 05.9. See [06.25 report](reports/06.25-puzzle-campaign-game-view.md).

## 06.26 — Campaign browser and link diagnostics (implemented)

Added a bounded, read-only campaign catalog API and **View → Puzzle Campaign Browser**. Valid documents show START, ordered levels, transition rules, and per-level file status; malformed documents and missing/invalid level files stay visible with diagnostics. Selecting a readable campaign populates the Game View ID field. The Editor does not modify campaign files. See [06.26 report](reports/06.26-puzzle-campaign-browser.md).

## 06.27 — Campaign authoring and safe level rename (implemented)

The campaign window edits a draft's identity, order, START and score/default/END rules, then confirms and validates writes through the project-file API. Level rename refuses when any readable campaign references the old ID, or when campaign references cannot be verified; update affected campaign links before renaming. See [06.27 report](reports/06.27-puzzle-campaign-authoring-rename.md).

## 04.12 — Editor C# consent gate (implemented)

The Play button requests one explicit, session-only approval when the current Scene has enabled ScriptBehavior components. It shows their exact bindings and verified build hashes, then recaptures before accepting. In 04.13 the runtime rechecks approval immediately before each unique host load and schedules callbacks; consent remains an execution policy, not a sandbox. The Inspector can add registered bindings disabled and supports enable/remove through transactional commands. See [04.13](reports/04.13-csharp-runtime-lifecycle.md) and [04.14](reports/04.14-scriptbehavior-editor-authoring.md).

## 04.14 — ScriptBehavior authoring in Inspector (implemented)

The selected entity Inspector lists existing script components and registry diagnostics. It can add an existing registered binding disabled, toggle it only when the build and binding metadata are current, or remove it. Typed commands carry undoable transactions; add/remove restore stable ComponentIds and component order. Scene v5 already stores this data, so no file migration is needed. Editing `.cs` source in an embedded Code workspace remains separate work. See [04.14 report](reports/04.14-scriptbehavior-editor-authoring.md).

- **04.18** Завершена design-оценка metadata-only проверки CLR и изоляции C# worker. Зафиксирована рекомендация отдельного процесса с ОС sandbox; implementation, package choice и новые функции не входят в шаг. См. [отчёт 04.18](reports/04.18-csharp-metadata-isolation-assessment.md).
- **04.19** Реализован Windows x64 metadata-validator worker prototype на `System.Reflection.Metadata` из .NET 10, с отдельным AppContainer-процессом, ограниченным protocol/input/output, Job Object и проверкой AppContainer file/network deny. Module initializer не запускается; Editor и AI пока не вызывают worker. См. [отчёт 04.19](reports/04.19-csharp-metadata-validator-worker.md) и [D084](DECISIONS.md#d084--windows-c-metadata-validator-worker-prototype-0419).
- **04.20 (реализован)** подключить AppContainer metadata worker к явной Project-panel диагностике зарегистрированных типов; ограничить assembly канонической папкой проекта, не менять Play host/ABI. Проверки и ограничения: [отчёт 04.20](reports/04.20-csharp-project-metadata-diagnostics.md), [D085](DECISIONS.md#d085--project-panel-c-metadata-diagnostics-0420).
- **04.21 (реализован)** измерена пакетная metadata-проверка; worker принимает до 64 типов за сессию, Editor разбивает до 256 типов по count/UTF-8 budget и показывает число сессий. Общий отчёт ограничен 128 KiB, без изменения sandbox/staging. См. [отчёт 04.21](reports/04.21-csharp-metadata-batch-performance.md) и [D086](DECISIONS.md#d086--c-metadata-validation-batch-limits-0421).
- **04.22 (завершён с ограничениями)** Выполнен Release-профиль доступного sample и найденной локальной Unity-сборки; Unity assembly устарела для одного скрипта и оба запрошенных типа не прошли AstroEngine ABI validation. Synthetic report smoke 128 KiB прокручивается и отрисовался за 3,27 ms. Это ограниченные данные, не benchmark большой игры; UI визуально не оценивался. Shared cache и изменения sandbox/ABI не обоснованы. Для Unity-style C# нужен отдельный product design. См. [отчёт 04.22](reports/04.22-csharp-metadata-project-ux.md) и [D087](DECISIONS.md#d087--04-22-c-metadata-profile-and-large-report-ux). Следующий пункт мастер-ТЗ — 05.1, требует отдельного старта.

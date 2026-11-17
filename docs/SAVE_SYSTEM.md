# Save System & Checkpoint Editor

Статус: требования раздела 06, runtime-реализации 05.4–05.5 и ограниченный editor prototype 06.4. Модуль обязателен для продукта, но игра ничего не сохраняет, пока вызывающий код явно не задаст профиль и не вызовет save/load. Правила включения в конкретную экспортируемую игру ещё предстоит определить с учётом P02.

## Реализованный SaveStore v1/v2 (05.4, 06.6)

`astro_runtime_save` предоставляет `SaveProfile` и `SaveStore`. Профиль создаётся пустым; каждое поле выбирается явно парой Entity ID + property ID. Поддерживаются `entity.name`, девять отдельных значений `transform.position|rotation|scale.{x,y,z}` и `sprite_renderer.visible`. Теги/группы, новые игровые компоненты (здоровье, инвентарь и т. п.), динамические ID и GUI authoring пока отсутствуют.

```cpp
auto profile = astro::runtime::SaveProfile::create("puzzle-progress", 1);
profile.value().addField(playerEntity, std::string(astro::runtime::SaveProperty::positionX));
profile.value().addField(playerEntity, std::string(astro::runtime::SaveProperty::entityName));

auto saves = astro::runtime::SaveStore::open(userProgressDirectory, 10);
auto written = saves.value().save(0, runtimeScene, profile.value(), "1.0.0");
auto loaded = saves.value().load(0, runtimeScene, profile.value(), "1.0.0");
```

Слоты имеют индексы от 0 до `slotCount-1`; имя файла генерируется store, а вызывающий код задаёт только корневую user-data directory. `inspect(slot)` выдаёт версию игры/профиля, Scene ID, дату Unix UTC и число выбранных значений. Максимум — 100 слотов и 16 MiB на файл. Размер по умолчанию — 4 MiB.

Формат `ASTRO_SAVE 1` содержит slot, Scene ID, profile ID/version, CRC32 fingerprint набора выбранных полей, game version, timestamp, типизированные записи и CRC32 всего содержимого. CRC32 обнаруживает случайное повреждение, но не защищает от читов и намеренного изменения с пересчётом checksum. Неизвестная версия, несовпадение game/profile version или изменённый selection set дают ошибку; миграций пока нет. При load проверяется весь документ до изменения Scene, затем изменения применяются к копии и публикуются только при успехе. Отсутствующие entity/component пропускаются и считаются в результате. Повторный load назначает значения и не создаёт дубликаты.

Для явно выбранных игровых данных, которые ещё не представлены свойствами Scene (например, очки Puzzle), доступен `SaveCustomState`: ключи — безопасные ASCII ID, значения — `string`, `double` или `bool`. `SaveProfile::addCustomField(key, typeExample)` объявляет каждый разрешённый ключ и его тип; перегрузки `save/load(..., customState)` требуют точного совпадения ключей и типов. Данные сериализуются как `ASTRO_SAVE 2`, включаются в fingerprint и checksum; Scene-only перегрузки остаются совместимыми с v1 и отклоняют профиль, если он требует custom state. Чтение v1 сохранено. Числа должны быть конечными, строка ограничена 64 KiB, ключ — 128 ASCII-байт. Это value map для вызывающего игрового кода, не автоматическая сериализация произвольного компонента или Puzzle board. Проверено round-trip для числа, bool и UTF-8 строки, ошибки selection/type и неизменность Scene/caller state при несовместимой загрузке; подробности — [отчёт 06.6](reports/06.6-save-custom-fields.md).

```cpp
using namespace astro::runtime;
auto profile = SaveProfile::create("puzzle-progress", 1);
profile.value().addCustomField("puzzle.level", 0.0);
profile.value().addCustomField("puzzle.unlocked", false);
SaveCustomState values{{"puzzle.level", 4.0}, {"puzzle.unlocked", true}};
saves.value().save(0, runtimeScene, profile.value(), "1.0.0", values);
saves.value().load(0, runtimeScene, profile.value(), "1.0.0", values);
```

## PuzzleSnapshot через выбранный SaveProfile (06.7)

`astro_runtime_puzzle_save` — optional adapter между Puzzle runtime и SaveStore. Он сериализует только снимок, полученный от `PuzzleGame::snapshot()`, в одно явно выбранное строковое поле `puzzle.snapshot`. Профиль может дополнительно выбирать обычные Scene-поля и другие custom fields. При загрузке адаптер применяет данные к Scene-копии, проверяет версию/структуру payload и вызывает `PuzzleGame::restore`; только после успеха публикует Scene и возвращает восстановленную PuzzleGame. Остальные custom values возвращаются отдельно в `additionalCustomState`.

```cpp
using namespace astro::runtime;
auto profile = SaveProfile::create("puzzle-progress", 1);
selectPuzzleSnapshotField(profile.value()); // явный выбор строки puzzle.snapshot
SaveCustomState additional{{"campaign.label", std::string("demo")}};
profile.value().addCustomField("campaign.label", std::string{});
auto written = savePuzzleSnapshot(store, 0, runtimeScene, profile.value(), "1.0.0", puzzle, additional);
auto restored = loadPuzzleSnapshot(store, 0, runtimeScene, profile.value(), "1.0.0");
```

Payload имеет собственную версию `ASTRO_PUZZLE_SNAPSHOT 1`; правила, прогресс, tile IDs в hex и состояние RNG записываются детерминированным UTF-8/ASCII текстом. Размер ограничен custom string SaveStore (64 KiB), доска — 4096 клеток. Неизвестная версия или malformed snapshot отклоняются без публикации Scene-кандидата. Миграций payload пока нет. Детали, проверки и ограничения: [отчёт 06.7](reports/06.7-puzzle-save-adapter.md).

### Ручные слоты Puzzle в Editor Game View (06.16–06.17)

Если Editor собран с `astro_runtime_puzzle_save`, активный Puzzle preview показывает slot selector (0–9) и `Save progress` / `Load progress`. В секции `Puzzle Save Settings` проекта можно изменить SaveProfile ID, schema version и относительную user-data подпапку; `Apply project settings` сохраняет их в `.astro/puzzle-save.settings`. Файл содержит настройки Editor, не игровые сохранения. Slot data остаётся под SDL preference root плюс относительный путь, не внутри checkout. Профиль выбирает ровно одно custom поле — `puzzle.snapshot`; Scene-свойства и другие custom values автоматически не выбираются. Автосохранения нет.

Для старых проектов без sidecar используются `puzzle-preview` / schema 1 и относительный путь, уникально вычисленный по нормализованному корню проекта; при первом сохранении настройки записываются рядом с другими `.astro` настройками. Directory override должен быть относительным, ограничен 512 UTF-8 байтами, absolute/rooted paths и `.`/`..` segments отклоняются. Изменение профиля/версии/папки не удаляет старые слоты. Старое общее хранилище `Saves/PuzzlePreview` не перемещается автоматически; его можно выбрать, указав эту относительную подпапку. Smoke использует временный каталог проекта и проверяет settings round-trip, отказ `..` и разные пути для разных проектов. Результаты: [отчёт 06.16](reports/06.16-puzzle-save-game-view.md), [06.17](reports/06.17-project-puzzle-save-settings.md).

Для authored Puzzle levels (06.18) текущий preview SaveProfile не сохраняет/загружает слоты, пока такой уровень запущен: слот не содержит level ID и поэтому не может безопасно проверить, к какой стартовой сетке относится snapshot. Обычный встроенный sample mode сохраняет прежнюю работу. Уровневый формат и авторинг описаны в [PUZZLE_LEVELS.md](PUZZLE_LEVELS.md); это ограничение снимается отдельным решением о per-level progress identity, не автоматическим выбором всего состояния.

Новая версия пишется во временный файл рядом со слотом, flush/fsync выполняется до атомарной замены. Перед заменой валидный предыдущий файл атомарно копируется в `.bak`. При повреждении основного файла load и inspect пробуют backup; восстановленное состояние автоматически не перезаписывает основной файл. Незавершённые временные файлы игнорируются. Тест на Windows удерживает destination без delete-sharing и проверяет, что неудачная публикация оставляет предыдущий слот доступным.

Включение модуля: `-DASTRO_BUILD_SAVE_RUNTIME=ON`; при `BUILD_TESTING=ON` target включается для тестов. Хранилище не выбирает платформенный каталог автоматически: приложение должно передать user-data directory. Пользователь сам отвечает за его размещение и backup вне одного слота.

`astro_runtime_puzzle_save` собирается отдельно при совместном включении `ASTRO_BUILD_PUZZLE_RUNTIME` и `ASTRO_BUILD_SAVE_RUNTIME`; тестовая конфигурация включает target автоматически. Базовый `astro_runtime_puzzle` не зависит от SaveStore.

## Реализованный Checkpoint v1 (05.5)

`CheckpointSystem` регистрирует runtime-only definition для существующей Entity с Transform. Activation area — axis-aligned 2D half-width/half-height вокруг текущей XY-позиции Entity; Z и parent/world transform не учитываются. Actor активирует checkpoint при переходе снаружи внутрь. Условие — optional C++ callback `Result<bool>(const Scene&, actorId)`. Отказ условия или ошибки профиля/записи дают `Rejected`; успешный переход публикует typed `Activated` event. Пока actor остаётся внутри, событие повторно не вызывается. При leave/re-enter `Repeatable` может сработать снова, `OneShot` остаётся потраченным.

Два режима разделены явно:

- `RespawnOnly`: переключает активную точку, не вызывает SaveStore и не пишет поля.
- `SaveSelectedProfile`: при активации требует явные `SaveStore` и существующий SaveProfile, записывает выбранные им поля в назначенный слот; ошибка отклоняет активацию.

Вызов `respawn` производится приложением после своей логики смерти. `RespawnRules` выбирает `PositionOnly` или `SelectedProfile`. По умолчанию — только позиция; выбранный профиль можно восстановить лишь для активного checkpoint в режиме `SaveSelectedProfile`. Сначала профиль применяется к Scene-копии, затем actor получает явную `respawnPosition`; при ошибке исходный runtime Scene не меняется. Без активного checkpoint используется заданный fallback position. Typed `Respawned` event публикуется после применения.

```cpp
astro::runtime::CheckpointSystem checkpoints;
astro::runtime::CheckpointDefinition definition;
definition.entity = checkpointEntity;
definition.area = {2.0, 1.5};
definition.respawnPosition = {10.0, 4.0, 0.0};
definition.mode = astro::runtime::CheckpointMode::SaveSelectedProfile;
definition.saveProfileId = "puzzle-progress";
definition.slot = 0;
checkpoints.registerCheckpoint(playScene, definition);

checkpoints.update(playScene, playerEntity, events, &saveContext);
checkpoints.respawn(playScene, playerEntity,
    {astro::runtime::RespawnRestoreMode::SelectedProfile, {}}, events, &saveContext);
```

Build option: `-DASTRO_BUILD_CHECKPOINT_RUNTIME=ON` (также включает `astro_runtime_save`); при `BUILD_TESTING=ON` модуль собирается для теста. Это не сериализованный Scene component, автоматическое обнаружение смерти, GUI/визуальный checkpoint, physics trigger, health/inventory respawn policy или no-code condition graph. Runtime caller должен регистрировать систему против Play Scene, не authoring Scene.

## Выбор сохраняемых данных

Разработчик сам выбирает объекты, компоненты и отдельные свойства, которые сохраняются. Автоматически сохранять весь мир без явного выбора профиля или правил запрещено.

SaveProfile — именованный набор правил с фильтрами по объектам, тегам и группам, перечнем сохраняемых свойств, исключениями и версиями.

Готовые профили предоставляются как редактируемые шаблоны:

| Шаблон | Поля |
| --- | --- |
| Персонаж | Позиция, здоровье, инвентарь, опыт |
| Транспорт | Позиция, топливо, повреждения, двигатель |
| Дверь | Открыта/закрыта |
| Головоломка | Уровень, звёзды |

Пользователь может изменить каждое поле. Состояния временных эффектов не сохраняются по умолчанию. Динамически созданным объектам необходимы стабильные runtime-ID и правила восстановления.

## Режимы сохранения

- Ручное сохранение в слот.
- Быстрые save/load.
- Автосохранение по событию, таймеру или переходу.
- Сохранение при активации checkpoint.

Во всех режимах состав сохраняемых данных определяется явным выбором профиля или правил.

## Checkpoint и respawn

Checkpoint — объект сцены с ID, областью активации, условиями, группами сохраняемых данных, точкой возрождения, однократностью или повторяемостью, визуальным представлением, звуком и событиями.

Предусмотрены отдельные режимы:

- «Только точка возрождения».
- «Запись полного выбранного состояния»: полнота относится только к явно выбранным данным, а не ко всему миру.

Правила respawn настраиваются отдельно: место, здоровье, инвентарь, время и враги. Активация checkpoint и восстановление при смерти не должны неявно подменять эти настройки.

## Формат и надёжность

Сохранение содержит версию игры и схемы, идентификаторы и метаданные. Требуются проверка целостности, атомарная запись через временный файл и безопасную замену, резервная копия, диагностика ошибок загрузки, миграции и контроль повреждений. Защита от читов не обещается.

Настройки сохранения находятся в проекте. Прогресс игроков хранится отдельно, в пользовательских каталогах целевой платформы. Предусмотреть слоты, лимиты, превью и дату сохранения.

Конкретные схемы, алгоритмы проверки целостности, платформенные операции безопасной замены, политика резервных копий и поведение при ошибках ещё не определены и не проверены.

## Инструменты редактора

Подэтап 06.4 добавляет окно [Save System Editor](SAVE_SYSTEM_EDITOR.md): авторинг in-memory профилей, явных групп Entity ID, правил выбора свойств, конфигураций checkpoint, XY preview и ручной isolated restore preview. При компиляции группы разворачиваются в явные поля runtime `SaveProfile`; весь мир автоматически не выбирается. Авторинг пока не сериализуется в проект. Поддержаны только свойства Save v1, перечисленные выше; GUI не добавляет health/inventory/fuel.

- «Активировать checkpoint».
- «Имитировать смерть».
- «Просмотреть сохраняемые поля».
- «Сравнить состояние до/после».

Настройка через GUI и команды должна быть эквивалентной. Для save.trigger и save.restore сохраняются runtime-контекст и ограничения обратимости из [ARCHITECTURE.md](ARCHITECTURE.md). Игровой checkpoint не следует смешивать с контрольной точкой отката редакторских/AI-изменений; конкретные контракты предстоит определить.

## Обязательные сценарии проверки

Проверки, выполненные для Save v1:

- Выборочная запись имени, Transform X и `SpriteRenderer.visible`; исключённая Transform Y и другие entity остаются без изменений.
- Round-trip выбранных полей и повторная загрузка без создания entity-дубликатов.
- Store reopen из того же user-data каталога, slot metadata/preview, пустой и выходящий за диапазон slot.
- Повреждённый файл/checksum, unsupported file version, game/profile version и изменение набора selected fields.
- Backup fallback после corruption, отсутствующий Entity/компонент, orphan временный файл.
- На Windows — отказ atomic replace при открытом destination без delete-sharing; прежний committed save остаётся загружаемым.

Остаются обязательными для следующих частей Save System:

- Сохранение и загрузку выбранного состояния.
- Пропущенный объект при восстановлении.
- Изменение схемы и миграции (v1 намеренно отклоняет неизвестную версию; мигратора ещё нет).
- Неверную версию сохранения и диагностику несовместимости.
- Повторную загрузку одного сохранения без непредусмотренного накопления состояния или дублирования объектов.
- Повреждённый файл и диагностику загрузки.
- Проверка восстановления при повторном запуске приложения/exported runtime.
- Смерть и respawn с настройками здоровья/инвентаря и выбранными группами полей (компоненты пока отсутствуют).
- Расширенные условия, звук, gameplay visualization и сериализованный checkpoint authoring.
- Полный отдельный process restart/export smoke; в 05.4 проверен reopen `SaveStore` на существующем user-data directory.
- Выборочную запись: выбранные поля сохраняются, исключённые не попадают в сохранение.

Дополнительно при проверке надёжности нужны сценарии прерывания записи и резервного восстановления; для динамических объектов — сохранность runtime-ID и корректность восстановления. Эти сценарии являются планом проверок, а не результатами выполненных тестов.

Общие требования к round-trip, checkpoint и отчётам проверок приведены в [TESTING.md](TESTING.md).

Зависимости реализации: стабильные ID, схема свойств, модель сцены, сериализация и миграции, Command API, runtime-контекст и платформенный файловый доступ. Учитывать Windows-пути, Unicode, UTF-8 и работу без сети. Новые библиотеки этим документом не выбираются.

## Puzzle authored-level slots (06.19)

For an authored Puzzle level, `selectPuzzleLevelIdentityField` adds the string custom field `puzzle.level.id` to the explicitly selected profile. `savePuzzleLevelSnapshot` records the requested safe level ID; `loadPuzzleLevelSnapshot` checks that value against the active ID and rejects mismatches before publishing the candidate Scene. Editor puts manual slots in `PuzzleLevels/<ID>` under the project's SDL user-data directory. The built-in Puzzle sample keeps its existing `puzzle.snapshot` profile and location. This does not add a separate save format, campaign progression, automatic writes, or migrations. See [PUZZLE_LEVELS.md](PUZZLE_LEVELS.md) and [06.19 report](reports/06.19-level-scoped-progress.md).

## Standalone Puzzle campaign progress (05.9)

The SDL Game UI campaign host provides a slot selector (0–9) and explicit **Save progress** / **Load progress** actions. It reuses `SaveStore` under the SDL user-data root at `PuzzleCampaigns/<FNV-1a key of canonical project path>/<campaign-id>`, keeping progress separate from project assets and distinguishing project roots. No slot is written automatically.

`selectPuzzleCampaignProgressFields` opts in to exactly four custom fields: `puzzle.campaign.id`, `puzzle.level.id`, `puzzle.campaign.ended`, and `puzzle.snapshot`. The demo currently selects no Scene properties. On load, profile/game versions, campaign identity, level membership, snapshot integrity, and compatibility with current authored board rules are checked before the copied Scene is published. A target-complete level resumes with Continue pending; a saved terminal END remains ended. Failed loads return diagnostics and leave Scene/session state unmodified. Schema mismatches are rejected; no migration exists.

This section describes the standalone SDL host integration. Editor Game View campaign persistence was added separately in 06.31; it uses the same SaveStore format and explicit campaign fields. Autosave, slot previews, save migration, and arbitrary custom campaign state remain unimplemented. See [PUZZLE_CAMPAIGNS.md](PUZZLE_CAMPAIGNS.md), [report 05.9](reports/05.9-campaign-progress-save.md), and [report 06.31](reports/06.31-editor-campaign-progress.md).

## Editor Game View Puzzle campaign progress (06.31)

The Windows Editor exposes manual campaign progress slots 0–9 beside the active Game View campaign. It reuses `PuzzleCampaignSaveAdapter` and the project's existing SaveProfile ID/version and user-data subdirectory settings. Files are stored below the SDL per-user data root at `<configured-subdirectory>/PuzzleCampaigns/<campaign-id>`; they never enter the project asset tree. The adapter explicitly selects campaign ID, level ID, END state, and Puzzle snapshot. No Scene properties or unrelated game state are selected.

Loading first validates the slot, campaign/level identity, snapshot integrity, and current authored board compatibility; only then is the session replaced. Failed loads leave active progress unchanged. The Editor smoke tests a manual save, a missing slot, and a successful round-trip. There is no autosave, migration, preview metadata, or physical mouse-click automation. See [report 06.31](reports/06.31-editor-campaign-progress.md).

### Campaign save namespace identity (06.33)

The Editor resolves the directory as `<SDL user-data root>/<userDataSubdirectory from .astro/puzzle-save.settings>/PuzzleCampaigns/<campaign-id>`. The path builder rejects unsafe relative settings and campaign IDs and performs no filesystem writes. A campaign ID change selects a different namespace; the old slots remain untouched and are not rewritten to the new campaign ID. Changing the project save subdirectory also selects a new namespace without moving files.

Moving a project together with its settings sidecar preserves the relative namespace, so existing slots remain available. Copying a project with that sidecar does the same and therefore causes both copies to share slots until one copy is configured with another user-data subdirectory. A project without a sidecar uses a default subdirectory derived from the current canonical project path. No slot discovery, migration, or move/copy UI is implemented. See [06.33 report](reports/06.33-campaign-save-identity.md).
# Campaign progress namespace and migration boundary

Editor campaign slots live below the configured project user-data namespace at `PuzzleCampaigns/<campaign-id>`. Project copies that carry `.astro/puzzle-save.settings` share that namespace; changing the setting selects another directory but does not move data. A changed campaign ID selects a different directory. See [D095](DECISIONS.md#d095--keep-campaign-save-namespaces-stable-and-never-migrate-implicitly-0633).

The current campaign save adapter validates the campaign ID embedded in the saved payload against the requested ID. Copying slot files after renaming a campaign therefore does not make them loadable and must not be presented as migration. The 06.34 assessment established this limitation; later 06.35–06.39 stages added a same-ID preview and explicitly confirmed copy workflow only. See [D096](DECISIONS.md#d096--do-not-copy-campaign-saves-across-campaign-identities-0634) and [06.34 assessment](reports/06.34-campaign-save-transfer-assessment.md).

`previewPuzzleCampaignSaveTransfer` now implements the read-only inspection portion for same-ID transfers. It uses `SaveStore::inspectExistingSlot` to examine slots 0–9 without exposing a writable store or creating the source directory, reports validated primary or backup-recovered metadata, identity mismatches, corrupt/unsupported records, source byte totals, destination primary/backup conflicts and unexpected source artifacts. It rejects parent traversal and symlink/reparse points in inspected paths. It does not create a missing target directory and has no copy/write action. A preview is not a migration approval and does not prove that a saved Puzzle snapshot matches the current authored campaign; the standard campaign load still performs that check. See [D097](DECISIONS.md#d097--keep-the-first-campaign-transfer-preview-read-only-and-same-id-0635) and [06.35 report](reports/06.35-campaign-save-transfer-preview.md).

The Editor's `Puzzle Save Settings` panel calls this API from **Preview save transfer**. It uses the currently applied settings as the source namespace and the uncommitted directory text as destination; changing that text clears the displayed result. Resolving the app data root reads `%APPDATA%` without creating it. The panel shows per-slot status/size/identity, primary and backup conflicts, and unexpected source artifacts. It does not persist draft settings or transfer data. Editor smoke checks the preview path, unchanged source file and absent destination. See [D098](DECISIONS.md#d098--show-campaign-save-transfer-preview-in-editor-read-only-0636) and [06.36 report](reports/06.36-campaign-save-transfer-editor-preview.md).

### Final read-only transfer preflight (06.37)

`revalidatePuzzleCampaignSaveTransferPreview` reruns inspection against the preview's canonical source and target paths. It compares slot classification, campaign/level state, byte totals, source primary/backup presence, bounded FNV-1a content fingerprints and unexpected source artifact names. A changed source returns `StaleSource`; a changed destination directory/collision state returns `StaleDestination`; an unchanged occupied target returns `DestinationConflict`. Any existing entry at an expected primary/backup target path counts as a conflict, including zero-byte files and directories. Invalid, mismatched or oversized source slots and unexpected source artifacts return `SourceNotTransferable`; empty numbered slots are skipped, but an entirely empty source is not ready. `Ready` is a read-only preflight result, not approval or a lock: files may change immediately afterward. FNV is not a cryptographic integrity check. Any future writer must revalidate immediately before acting, create target files exclusively, preserve source bytes and request explicit per-operation confirmation. No writer exists. See [D099](DECISIONS.md#d099--revalidate-campaign-save-transfer-preview-before-write-0637) and [06.37 report](reports/06.37-campaign-save-transfer-preflight.md).

### Copy-only transaction API (06.38)

`copyPuzzleCampaignSaveTransfer` takes an explicit `explicitlyConfirmed` caller flag. False returns `ConfirmationRequired` before filesystem work. True reruns 06.37 preflight, then copies each primary and `.bak` artifact for valid or backup-recovered same-ID slots verbatim. It never copies unknown artifacts, and never migrates payloads. Windows uses `CreateFileW(CREATE_NEW)`; POSIX uses `open(O_CREAT|O_EXCL)`. Existing destination paths are never overwritten. The optional progress callback can cancel the batch.

On cancellation or detected I/O failure, rollback removes only outputs created by that attempt if their bytes still match the expected prefix, then removes only empty directories created by that attempt. Competing destination data is preserved. The result distinguishes complete rollback from `RollbackIncomplete` and lists paths left for inspection. Source slots remain unchanged. This is not crash-atomic: process/power failure during a multi-file copy may leave partial files in the target. Runtime API integration tests use isolated temporary fixtures. See [D100](DECISIONS.md#d100--same-id-copy-only-campaign-save-transaction-0638) and [06.38 report](reports/06.38-campaign-save-copy-transaction.md).

### Editor confirmation workflow (06.39)

After a read-only preview, **Copy campaign saves…** opens a separate confirmation dialog. Review the exact source and target paths, eligible slot count, byte total, and no-overwrite statement. Only **Copy reviewed save files** calls the API with explicit confirmation; **Cancel copy** performs no copy. The API revalidates the preview before writing, so a stale source/target or collision is reported without replacing existing data. The result area reports files and bytes copied, failure/cancellation/rollback state, diagnostic text and paths left after `RollbackIncomplete`. Changing the destination folder or refreshing the preview clears the prior result. This workflow copies same-ID primary/backup files byte-for-byte; it does not migrate, merge or overwrite saves. A process/power interruption can leave a partial target set. Editor smoke uses only a unique temporary Unicode fixture, opens the confirmation dialog, exercises the same confirm handler, and checks that the source remains and copied slot bytes match. See [D101](DECISIONS.md#d101--editor-confirmation-before-campaign-save-copy-0639) and [06.39 report](reports/06.39-campaign-save-transfer-editor-confirmation.md).

### Invalid transfer journal diagnostics (06.43)

Recovery inspection distinguishes a journal that cannot be read, has an unsafe/non-regular path, exceeds the 4096-byte limit, or is malformed/truncated/unsupported-version. Invalid journals do not yield target artifact names or paths. The Editor shows the category and underlying read/parser diagnostic and leaves the journal and any slot-looking files untouched. Copy, resume and acknowledgement remain unavailable until manual inspection. Tests cover unsupported version, truncation, over-limit size, and a directory at the journal path. This is diagnosis only; no journal repair or cleanup is attempted. See [D105](DECISIONS.md#d105--diagnose-invalid-campaign-transfer-journals-0643) and [06.43 report](reports/06.43-invalid-campaign-transfer-journal-diagnostics.md).

### Archive a readable malformed journal (06.44, behavior revised in 06.45)

For `Malformed` records readable within the journal size limit, Editor offers separate confirmation to create an archive copy. Runtime validates the displayed preview, acquires the source journal lifecycle lease, compares exact bytes through the held handle, writes and flushes a unique adjacent staging file, then verifies it through the same handle before publication. Windows uses handle-based no-replace rename; POSIX uses same-directory `linkat` no-replace publication and removes the staging entry only after verifying the final inode. A fingerprint-derived final archive path is accepted only with identical contents; different occupants are preserved and block the action. The source journal deliberately remains in place and continues blocking transfer/resume until manual review. An interrupted process can leave a staging file; it is preserved for inspection and does not block later attempts, which choose a new unique staging name. Existing partial/conflicting final archives still fail closed and require manual review. Save slots and other possible campaign artifacts are never read or changed. `Unreadable`, `Unsafe`, and `Oversized` cases stay manual. POSIX locking is advisory and coordinates only processes following the same protocol; filesystem crash durability varies. Subprocess tests terminate at partial-write, pre-publish and post-publish boundaries on Windows/POSIX, but do not simulate kernel crash or power loss. See [D106](DECISIONS.md#d106--archive-readable-malformed-campaign-journals-0644), [D107](DECISIONS.md#d107--preserve-malformed-journals-during-archive-0645), [D111](DECISIONS.md#d111--lock-malformed-journal-archive-0648), [D113](DECISIONS.md#d113--staged-atomic-no-replace-journal-archive-publication-0650), [D114](DECISIONS.md#d114--subprocess-interruption-tests-for-staged-journal-archive-0651), and [06.51 report](reports/06.51-archive-interruption-tests.md).

06.52 reviewed legacy files at the deterministic final archive path. An exact bounded regular-file copy can be reused. A different or unreadable occupant is not proof of an interrupted archive: even a file whose bytes match a prefix may be unrelated user data. The runtime leaves it untouched and fails closed. Since 06.53, Editor can report the expected path, no-follow object kind, bounded byte size and comparison result. Manual resolution remains outside the engine; do not delete, move, truncate or overwrite the conflicting file automatically. A “prefix match” does not establish provenance. See [D115](DECISIONS.md#d115--manual-review-of-legacy-conflicting-archive-files-0652), [D116](DECISIONS.md#d116--read-only-diagnostics-for-archive-conflicts-0653), [06.52 assessment](reports/06.52-legacy-archive-conflict-assessment.md) and [06.53 implementation](reports/06.53-archive-conflict-diagnostics.md).

06.54 assessed a possible explicit **copy-aside** action for a conflicting archive path. In 06.55 the Editor gained this action for a differing, readable regular file no larger than 4096 bytes. Preview writes nothing and reports the exact source path, generated adjacent destination and byte count. The user must confirm in a dialog that also says the source's author is unknown and the result is unverified. The operation rechecks and holds the same no-follow source plus recovery journal, stages in the same directory and publishes atomically without replacing a destination. Links/reparse points, directories, unsupported types, unavailable and oversized files fail closed. Cancel and stale previews do not create output.

The original conflicting file, malformed journal and save slots remain untouched. The copy is raw evidence only: it is not parsed as a valid archive, does not clear the conflict and does not trigger retry. A terminated process can leave a uniquely named staging file, or a fully published copy, for manual inspection. POSIX `flock` coordinates cooperating processes only; this is not protection against arbitrary hostile local writers or a claim of power-loss durability. See [D118](DECISIONS.md#d118--explicit-bounded-copy-aside-for-archive-conflicts-0655) and [06.55 report](reports/06.55-conflict-copy.md).

06.56 verifies exact-byte copy and preservation of the source, malformed journal and candidate slot under a Unicode parent directory on Windows and WSL/POSIX. The Editor confirmation's paths, size and warning were visually reviewed in a smoke-owned temporary project; Cancel produced no copy and exited successfully. Other POSIX/macOS/mobile filesystems were not exercised. See [06.56 report](reports/06.56-unicode-copy-and-dialog-review.md).

### Interrupted transfer recovery (06.40)

Before writing slot artifacts, the runtime creates and flushes a versioned `.astro-campaign-transfer.pending` record with the campaign ID and planned slot artifact sizes/fingerprints. Copy holds the exclusive journal lease until commit/rollback; resume holds it through final target verification; acknowledgement first checks the displayed journal and artifacts, then acquires the lease, compares the exact bytes again and removes only that journal object/entry. Windows uses `LockFileEx` plus handle-based deletion. POSIX uses `openat` with `O_NOFOLLOW`, advisory `flock`, directory-relative inode verification and `unlinkat`. POSIX locks coordinate only cooperating processes; this is not a boundary against arbitrary local writers. Save slot artifacts are never removed or rewritten by acknowledgement. After interruption, preview reports each target as complete, partial, missing, changed or unsafe. Resume skips complete artifacts and exclusively creates missing ones; changed sources or targets stop the operation. Partial files stay byte-for-byte untouched because the journal cannot prove their provenance. Invalid journals require manual review. These transactions are not crash-atomic across multiple save files and do not guarantee durability through sudden power loss. Integration tests use Unicode temp paths, child-process interruption, active-lock contention, stale-source refusal, and partial-file preservation. See [D102](DECISIONS.md#d102--journal-and-report-campaign-save-transfer-interruption-0640), [D103](DECISIONS.md#d103--explicit-resume-for-campaign-save-transfers-0641), [D104](DECISIONS.md#d104--preserve-partial-campaign-save-targets-0642), [D110](DECISIONS.md#d110--cross-platform-campaign-journal-lifecycle-lock-0647), and [06.47 report](reports/06.47-posix-journal-acknowledgement-readiness.md).


### macOS/POSIX durability assessment (06.57)

The POSIX copy-aside path stages and publishes in the same directory with descriptor-relative operations and attempts `fsync` on file and directory handles. Apple documents that ordinary `fsync` can leave data in drive caches and does not assure survival of power loss; `F_FULLFSYNC` is an Apple-specific stronger request with filesystem/device limitations. Advisory `flock` prevents races only among cooperating clients. macOS/APFS directory flush behavior and the current implementation have not been tested on a Mac, so no macOS durability guarantee is made. See [D120](DECISIONS.md#d120--macos-posix-copy-aside-portability-assessment-0657), [Apple fsync(2)](https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man2/fsync.2.html), [Apple fcntl(2)](https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man2/fcntl.2.html), and [06.57 assessment](reports/06.57-posix-macos-portability-assessment.md).

The Mac test build also exposed two test-only portability errors: direct formatting of `file_time_type::duration::rep` selected an ambiguous `std::to_string` overload. 06.58 now converts those clock counts explicitly to `long long`; Windows and WSL Release suites pass. A Mac rebuild is still needed to confirm the compiler-specific fix. This change does not alter save-system code or its durability status; see [06.58 report](reports/06.58-macos-test-portability-fix.md).

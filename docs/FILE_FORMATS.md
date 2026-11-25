# Формат файлов сцен AstroEngine

## Astro Scene v11

`*.astro-scene` — UTF-8 текстовый формат редакторской Scene/Entity модели.

```text
ASTRO_SCENE 11
SCENE <scene-id> "<quoted scene name>"
ENTITY <entity-id> <parent-id-or-0> "<quoted entity name>"
COMPONENT <component-id> <entity-id> "astro.Transform" 1 <position xyz> <rotation-degrees xyz> <scale xyz>
COMPONENT <component-id> <entity-id> "astro.SpriteRenderer" 1 <asset-id> <visible-0-or-1>
COMPONENT <component-id> <entity-id> "astro.ScriptBehavior" 1 <script-asset-id> <enabled-0-or-1> "<managed-type-name>"
COMPONENT <component-id> <entity-id> "astro.MeshRenderer" 2 <model-asset-id> <mesh-index> <primitive-index> <visible-0-or-1> <material-override-asset-id-or-0>
COMPONENT <component-id> <entity-id> "astro.PrimitiveRenderer" 2 <kind-1-4> <visible-0-or-1> <material-override-asset-id-or-0>
COMPONENT <component-id> <entity-id> "astro.Camera3D" 2 <vertical-fov-degrees> <near-plane> <far-plane> <active-0-or-1>
PREFAB_INSTANCE <root-entity-id> <prefab-id> <source-link-count> <override-count>
PREFAB_LINK <root-entity-id> "<source-key>" <entity-id>
PREFAB_OVERRIDE <root-entity-id> "<source-key>" <property-kind> <typed-value>
END
```

Имена и type ID записываются средствами `std::quoted`; числа разбираются с classic locale. Для каждой сущности требуется ровно одна запись Transform версии 1. Scene ID, Entity ID и Component ID ненулевые и не могут достигать максимума своего unsigned типа. Entity ID и Component ID не повторяются внутри сцены; Component ID глобально уникален для этой сцены. Родитель должен существовать, циклы запрещены. Transform числа должны быть конечными. Position и rotation по умолчанию нулевые, scale — (1,1,1). Rotation — локальные углы Эйлера в градусах; композиция и мировые координаты не реализованы.

Файл `ASTRO_SCENE 11` ограничен 16 MiB и 100000 сущностями. Каждая сущность содержит ровно один Transform; SpriteRenderer, MeshRenderer, PrimitiveRenderer и Camera3D необязательны; ScriptBehavior может отсутствовать или встречаться несколько раз. MeshRenderer и PrimitiveRenderer взаимоисключающие. PrimitiveRenderer v2 хранит тип встроенной геометрии: Cube (1), Plane (2), Sphere (3), Cylinder (4), visible и необязательный project Material AssetId (0 означает отсутствие переопределения). Геометрия генерируется в памяти. Scene v10 PrimitiveRenderer v1 мигрирует с material ID 0; чтение не переписывает файл. Camera3D v2 хранит FOV в диапазоне (0,180), clipping planes 0 < near < far и active-флаг; допускается не более одной активной камеры. Scene v8 Camera3D v1 загружается с active=false. MeshRenderer v2 хранит ненулевой Model AssetId, mesh/primitive indices, visible и необязательный project Material AssetId. MeshRenderer v1 в Scene v6 получает пустой override при чтении. ScriptBehavior v1 хранит ненулевой AssetId, валидное имя managed type и enabled-флаг. Prefab metadata содержит root, PrefabId, source-key map и типизированные overrides. Неизвестный компонент/override, версия, повреждённый UTF-8, неверная структура, повторные ID, отсутствующий владелец/родитель/source key и лишние записи возвращают ошибку; данные не отбрасываются молча. Форматы Scene v1–v10 читаются; переход в v11 происходит только после явного сохранения.

Сохранение сериализует в уникальный временный файл рядом с назначением, затем атомарно заменяет его (Windows `MoveFileExW` с replace/write-through). Это не гарантирует восстановление после аварийного завершения до замены, резервные копии, checksum или одинаковую долговечность на всех файловых системах.

## Миграция Astro Scene v1/v2/v3/v4/v5 → v6

Загрузчик читает v1 без компонентов, v2 с Transform, v3 с Transform/SpriteRenderer, v4 с prefab instances, v5 с ScriptBehavior и v6 с MeshRenderer. Старые версии не получают MeshRenderer. При чтении v1 создаются Transform версии 1 с новыми Component ID и значениями по умолчанию. Сохранение загруженной старой версии пишет v6; чтение само по себе исходный файл не переписывает.

## Границы

На этапах 03.3–03.4 реализованы Transform, SpriteRenderer, prefab instance links и overrides. В 04.7 добавлено сохранение ScriptBehavior references; 07.1c добавляет MeshRenderer с model AssetId. Он пока не подключает 3D геометрию к Editor/Game View renderer. Prefab capture явно отклоняет Scene с MeshRenderer до расширения prefab-формата. Scene не является форматом прогресса игры или Save System.

## Prefab asset v1

`<project>/Prefabs/<prefab-id>.astro-prefab` — UTF-8 файл с `ASTRO_PREFAB 1`, записью PREFAB (числовой ID и quoted name), NODE records и END. NODE хранит stable local ID, parent local ID (0 только у корня), имя, Transform, необязательную ссылку SpriteRenderer (AssetId/visible) и необязательный nested PrefabId. Требуется ровно один root, уникальные local IDs, валидные родители/Transform/name; лимиты — 16 MiB, 100000 node records и 256 уровней локальной иерархии. Файл пишется через уникальный временный файл и атомарную замену.

Вложенная prefab ссылка разворачивается как дочернее дерево под узлом-владельцем. Цикл graph и глубина свыше 64 отклоняются до изменения Scene. Source key — полный путь числовых PrefabId/localId от корневого ассета; обновление сопоставляет эти ключи и сохраняет EntityId. Instance source map и overrides записываются в Scene v5 (читаются и из v4). Миграция prefab-файла пока не нужна (поддерживается только v1); Editor UI для создания/обновления и конфликтный merge отсутствуют.

## Asset index v1–v4

`<project>/.astro/assets.index` — UTF-8 line-based registry manifest:

```text
ASTRO_ASSETS 4
NEXT <next-asset-id>
ASSET <asset-id> "<Assets-relative UTF-8 path>" PNG|JPEG <width> <height> <source-size> <sha256-hex>
SCRIPT <asset-id> "<Scripts-relative .cs path>" "<managed-type-name>" "<Library/Scripts-relative .dll path>" "<Library/Scripts-relative .runtimeconfig.json path>" <source-sha256>
MODEL <asset-id> "<Assets-relative .gltf|.glb path>" <source-size> <source-sha256> <import-content-sha256>
MATERIAL <asset-id> "<Assets-relative .astro-material path>" <material-schema-1-or-2> <source-size> <source-sha256>
END
```

Индекс v4 содержит общий монотонный AssetId для image, script, model и material records. Reader принимает v1–v4; открытие старого индекса само его не меняет, следующая явная регистрация пишет v4. MODEL path — относительный к `Assets/`, только `.gltf/.glb`; исходный основной файл ограничен 8 MiB и хранит размер/SHA-256 плюс digest импортированного содержимого. `registerModel` принимает уже успешно импортированную геометрию, а `resolveModel` проверяет основной файл и его hash. Изменение внешнего glTF buffer обнаруживается повторным `GltfImporter::importAndRegister`, который обновляет content digest; отдельного dependency record для каждого внешнего buffer пока нет. MATERIAL path — относительный к `Assets/Materials/`, только `.astro-material`; запись содержит schema version, размер и SHA-256. `resolveMaterial` проверяет безопасное нахождение источника в проекте, размер/hash, строгий парсинг и то, что все texture AssetIds указывают на зарегистрированные image records. Script source обязан находиться под `Scripts/`, быть UTF-8 `.cs` не больше 16 MiB; сборочные outputs — в `Library/Scripts/`. Дубликаты ID/source/type, неверный hash/размер/путь/format, неизвестная версия, лишние строки и повреждённый UTF-8 отклоняются. Индекс ограничен 16 MiB, 100000 entries и 4096 UTF-8 байт на путь; публикация атомарна. Повреждённый индекс остаётся нетронутым и возвращает ошибку.

## Material asset v1 and v2 (07.2b, 07.2o)

`Assets/Materials/<name>.astro-material` — UTF-8 line format. Первая строка `ASTRO_MATERIAL 1`; далее обязательные строки параметров в фиксированном порядке, ноль-пять TEXTURE records, затем `END`:

```text
ASTRO_MATERIAL 1
BASE_COLOR 1 1 1 1
METALLIC 1
ROUGHNESS 1
EMISSIVE 0 0 0
ALPHA_MODE OPAQUE
ALPHA_CUTOFF 0.5
DOUBLE_SIDED 0
NORMAL_SCALE 1
OCCLUSION_STRENGTH 1
TEXTURE_COUNT 1
TEXTURE BASE_COLOR 12 0
END
```

Texture roles are `BASE_COLOR`, `METALLIC_ROUGHNESS`, `NORMAL`, `OCCLUSION`, and `EMISSIVE`. Each binding stores a nonzero registered image AssetId and a non-negative `texCoord` selector. Both versions validate base-color RGBA, metallic, roughness, emissive RGB and alpha cutoff in `[0,1]`, normal scale finite and non-negative, and occlusion strength in `[0,1]`. Defaults are white base color, metallic/roughness 1, black emissive, OPAQUE, cutoff 0.5, single-sided, normal scale 1, occlusion strength 1, and no texture bindings. Floating values use classic locale and finite values only. Duplicate/unknown roles, unknown rows, malformed or extra data, unsupported schema and files over 64 KiB are rejected.

Schema v1 remains readable and resolves to the historical project-preview settings: Clamp to edge on U/V, Nearest min/mag filtering and identity UV transform. Reading/registration does not rewrite v1 source bytes. Schema v2 appends these two required rows after the texture bindings and before `END`:

```text
BASE_COLOR_SAMPLER MIRRORED_REPEAT REPEAT LINEAR NEAREST
BASE_COLOR_UV_TRANSFORM 0.1 -0.1 0.15 1.2 0.8
```

Wrap enums are `REPEAT`, `MIRRORED_REPEAT`, `CLAMP_TO_EDGE`; minification is one of `NEAREST`, `LINEAR`, `NEAREST_MIPMAP_NEAREST`, `LINEAR_MIPMAP_NEAREST`, `NEAREST_MIPMAP_LINEAR`, `LINEAR_MIPMAP_LINEAR`; magnification is `NEAREST` or `LINEAR`. UV transform values are offset U/V, counter-clockwise radians, scale U/V; each must be finite and within +/-1,000,000. New materials default to schema v2. An edited v1 material upgrades only on explicit Inspector save. The save validates the current source hash first, writes via a flushed temporary file and atomic replacement, then refreshes the same AssetId's index record; externally changed sources are rejected rather than overwritten.

Base-color and emissive factors are linear values; base-color and emissive image samples are intended for sRGB decoding. Metallic-roughness texture convention follows glTF: G is roughness and B is metallic in linear data. Normal and occlusion are linear; occlusion uses R. Alpha MASK uses `alphaCutoff`; OPAQUE/BLEND interpretation and rendering are not implemented yet. The values are already validated and serialized/imported; the rendering gap and recommended depth/blend policy are assessed in [07.2q](reports/07.2q-alpha-blending-depth-assessment.md). These are semantic import/render conventions; the current format/parser does not implement generalized PBR or enforce a render queue. The optional Windows Scene View decodes and previews base-color only, applies wrap/filter and UV0 transform for source and project materials, and generates bounded linear-space mip chains when the selected minification mode requests them. AssetRegistry registration reads the file but never edits it; explicit `saveMaterial` retains stable IDs. Resolve detects source hash/size drift and unregistered image references. It does not yet verify that each referenced image file still matches its own registry hash.

07.2h adds a separate Windows BGFX experiment that samples only `BASE_COLOR` from a registered image through `TEXCOORD_0`. The fixture's factor is white to isolate sampling. This experiment does not define a renderer color-space contract: it does not apply sRGB conversion or multiply the linear baseColorFactor. All format roles above remain data semantics, not general texture-role renderer support.

07.2i defines the intended base-color semantics: source image RGB is sRGB encoded, alpha is linear, and `baseColorFactor` is linear RGBA. Its isolated Windows sample decodes source RGB by the IEC sRGB transfer curve, multiplies the factor, and quantizes a temporary linear BGRA8 texture payload; BGFX writes to an sRGB backbuffer. This does not change `.astro-material`, source assets, or cache bytes, and it does not imply that other renderers honor these semantics yet. 07.2cb later documented a transfer mismatch in Editor/Runtime BGFX, corrected and numerically checked by 07.2cc. Other renderers remain unverified. See [assessment 07.2cb](reports/07.2cb-material-color-space-contract.md) and [07.2cc result](reports/07.2cc-bgfx-color-transfer.md).

## glTF material import metadata (07.2c)

This is an in-memory `GltfImportResult`, not a new persisted project file. Each imported mesh has `primitiveMaterialIndices` parallel to its geometry primitive array; an empty optional means glTF supplied no material, otherwise the value indexes `GltfImportResult::materials`. Material records preserve supported core metallic-roughness factors and texture-role references as `(textureIndex, texCoord)` plus optional `KHR_texture_transform` offset/rotation/scale. The transform's optional texCoord override takes precedence. Texture records point to source image and sampler indices; the sampler table preserves wrap modes and optional filters. Image records retain source kind, name, MIME type, safe project-relative path or opaque embedded reference, availability, and source size/hash where bytes are available. External local image bytes are capped at 64 MiB each and 128 MiB total and contribute to `contentHash`; they are read-only and not decoded. `data:` or GLB buffer-view images remain opaque references/hashes.

The geometry cache was `ASTROMSH` v1 through 07.2f. In 07.2g it advances to v2 and stores per-primitive channel flags plus POSITION, optional NORMAL and optional TEXCOORD_0 geometry; caches use a `.v2.astro-mesh-cache` filename, leaving v1 files untouched and unused. Material/image/sampler tables and per-primitive material assignments are reconstructed from the glTF document each import; the content-derived cache key includes external image identity/hash so its import digest detects image changes. Missing local images are kept with diagnostics. Remote image URIs are preserved as unsupported metadata and never fetched. Unsafe traversal, reparse points, or resolved paths outside `Assets/` are rejected. Core metallic-roughness, alpha, normal scale and occlusion strength are validated; unsupported material extensions/properties produce diagnostics. `KHR_texture_transform` and core sampler values are preserved as import metadata; the optional Windows Scene View applies them only to source base-color UV0. Other texture roles are not rendered. No image AssetIds are created by this importer, and no material file or source resource is modified.

The current mesh vertex contract keeps `hasNormals` and `hasTexCoords` independently of default-initialized channel values. `NORMAL` must be a matching, bounded, non-sparse FLOAT VEC3 with finite, non-zero values. `TEXCOORD_0` must be a matching, bounded, non-sparse FLOAT VEC2 or normalized UNSIGNED_BYTE/UNSIGNED_SHORT VEC2; integer UVs are converted to float coordinates. POSITION remains a bounded non-sparse FLOAT VEC3. Other attributes, including TEXCOORD_1+, remain unsupported and are rejected rather than dropped. The optional Editor Scene View samples UV0 for base color; normals and other UV sets are not used by that pass.

## C# project build manifest/state (04.9–04.10)

Project-owned UTF-8 JSON at `.astro/csharp-build.json`:

```json
{
  "schemaVersion": 1,
  "sdkVersion": "10.0.404",
  "targetFramework": "net10.0",
  "assemblyName": "MyGameScripts",
  "scriptBindings": [
    { "sourcePath": "Scripts/SceneMotion.cs", "managedTypeName": "MyGame.SceneMotion" }
  ]
}
```

`scriptBindings` is optional; when present it must be an array of objects with exactly `sourcePath` and `managedTypeName`. Each source must exactly name one compiled `.cs` file below `Scripts/`, and may appear once. Managed names are 1–512 valid UTF-8 bytes without whitespace/control characters. The prototype requires schema version 1, a pinned `.NET 10.0.x` SDK, `net10.0`, and a simple assembly name without path separators. It compiles regular UTF-8 `.cs` files below `Scripts/`, with bounded source count/size, and disables all NuGet package sources. Paths through reparse points are rejected. It does not modify project source files.

On success, ignored `Library/Scripts/build-state.json` schema v2 contains the input fingerprint, pinned SDK/framework/assembly name, output paths/hashes and the relative path/hash of `asset-bindings.state`. Outputs are published into a new immutable `Library/Scripts/<assemblyName>/<fingerprint>-<generation-id>/` directory; earlier generations are retained. The state file is written through a sibling temporary and replace only after both required outputs exist.

`Library/Scripts/asset-bindings.state` is a UTF-8 line format, header `ASTRO_CSHARP_PROJECT_BUILD 1`; it records fingerprint, manifest/output hashes, SDK/framework/assembly metadata, complete source path/hash set, selected binding count and quoted `BINDING "<sourcePath>" "<managedTypeName>"` records, then `END`. It is generated by the builder and must match the sidecar hash in build-state schema v2. AssetRegistry validates exact source set and hashes plus contained output paths and hashes. `inspectScriptBuild()` reports `Missing`, `Current`, `Stale`, or `Invalid` and registry sync diagnostics. `registerScriptsFromBuild()` is an explicit transaction that updates the Asset index only after every record validates; existing source paths retain their AssetId. This is derived local cache/handoff, not game save data. It validates type-name structure but does not load the assembly to confirm that a CLR type exists; that happens only on explicit host load. Failed restore/compile does not publish a new successful state. Source/config changes, changed SDK, missing output, or output hash mismatch make `-Check` report stale.

Кэшированный image находится в `Library/Assets/<sha256>.v1.<png|jpg>`. Сейчас он побайтно равен encoded source и восстанавливается через импорт; старые хэш-версии не удаляются. Managed `.dll`, `.runtimeconfig.json`, `build-state.json` и `asset-bindings.state` — производные build outputs/cache и не добавляются как игровые ассеты; `.astro/assets.index` хранит выбранные Script AssetIds. `CSharpScriptHost::loadScriptAsset` использует resolved assembly/type и ожидает managed entry point `FixedUpdate`; host остаётся optional Windows x64 prototype.

## Asset dependencies v1 и package v1 (03.5)

`.astro/assets.dependencies` — UTF-8 text: `ASTRO_DEPENDENCIES 1`, затем отсортированные `NODE <asset-id> <count>` и `DEP <asset-id>` строки, завершённые `END`. Циклы, нулевые ID, дубликаты, неизвестная версия, неверная структура и лишние данные отклоняются. Лимиты: 16 MiB, 100000 узлов, 500000 связей. Граф содержит только явно записанные AssetId связи.

`Library/Derived/<platform>/<sha256>.v1.astro-pack` — бинарный детерминированный контейнер: ASCII `ASTRO_PACK`, little-endian version (u32), длина и UTF-8 platform tag, число записей, далее для каждой записи ID (u64), формат (u32: 1 PNG/2 JPEG), width/height (u32), длина (u64), 64 ASCII hex SHA-256 и payload. Порядок записей — возрастание AssetId; предел — 1 GiB. Hash включает target tag и все package bytes. Atomic replacement используется для публикации; аварийный временный файл может сохраниться. Target tags не подтверждают платформенную совместимость или оптимизацию.

## Save slot v1 (05.4)

`<user-data-root>/slot-<index>.astro-save` — UTF-8 text; `<user-data-root>` задаёт приложение, индекс — целое 0-based значение в диапазоне настроенного SaveStore. Имя файла не принимается от пользовательского slot input. Максимум 100 слотов; по умолчанию каждый ограничен 4 MiB, жёсткий максимум configurable — 16 MiB.

```text
ASTRO_SAVE 1
META <slot> <scene-id> "<profile-id>" <profile-version> <profile-fingerprint-crc32> "<game-version>" <unix-utc-seconds>
RECORDS <count>
FIELD <entity-id> "<property-id>" S "<utf8-string>"
FIELD <entity-id> "<property-id>" D <finite-double>
FIELD <entity-id> "<property-id>" B <0-or-1>
END
CHECKSUM <crc32-hex>
```

`FIELD` записывает только значения, перечисленные SaveProfile для точного Entity ID. Поддерживаемые property IDs: `entity.name`, `transform.position.x/y/z`, `transform.rotation.x/y/z`, `transform.scale.x/y/z`, `sprite_renderer.visible`. Fingerprint включает profile ID/version и выбранный набор entity/property; изменение набора без version bump также отклоняется. Дубликат поля, неверный typed value, неизвестная запись/версия, лишний хвост, checksum mismatch, несовпадающая Scene/game/profile выдаются как ошибка; формат v1 не мигрируется автоматически. CRC32 — только detection случайного повреждения, не мера защиты от читов.

Профиль с явно выбранными custom fields записывается как `ASTRO_SAVE 2`; формат сохраняет те же `META` и `FIELD` записи, затем содержит:

```text
CUSTOM_RECORDS <count>
CUSTOM "<safe-key>" S "<utf8-string>"
CUSTOM "<safe-key>" D <finite-double>
CUSTOM "<safe-key>" B <0-or-1>
END
CHECKSUM <crc32-hex>
```

Каждый custom key и его тип должны быть заранее выбраны в SaveProfile, а переданные значения должны в точности соответствовать набору ключей. Профили без custom fields продолжают записываться как v1; reader принимает v1 и v2. Для T01 Puzzle optional `astro_runtime_puzzle_save` помещает снимок в выбранный строковый ключ `puzzle.snapshot`; его отдельный payload version 1 (`ASTRO_PUZZLE_SNAPSHOT 1`) содержит размеры/правила, прогресс, tile IDs и RNG-state. Это не формат сцены, а выбранное поле пользовательского save slot; описание и ограничения: [SAVE_SYSTEM.md](SAVE_SYSTEM.md).

Встроенный Windows Puzzle preview в Editor открывает 10-слотовый SaveStore под каталогом, который возвращает `SDL_GetPrefPath("AstroEngine", "AstroEngine")`; per-project relative subdirectory описан ниже. Исторический общий путь `Saves/PuzzlePreview` не используется по умолчанию, но может быть выбран в настройках для доступа к старым слотам.

## Puzzle project settings v1 (06.17)

`<project>/.astro/puzzle-save.settings` — UTF-8 text sidecar, optional for existing projects; `project.astro` remains `ASTRO_PROJECT 1`. Missing settings use `PROFILE "puzzle-preview" 1` and a project-specific relative folder generated from the normalized project root. On the first manual save the defaults are written to the sidecar.

```text
ASTRO_PUZZLE_SAVE_SETTINGS 1
PROFILE "<safe-profile-id>" <schema-version>
DIRECTORY "<user-data-relative-directory>"
END
```

Profile ID is 1–64 ASCII letters/digits/dot/underscore/hyphen; schema version is a nonzero u32. Directory is 1–512 UTF-8 bytes, relative to SDL user preferences; rooted/absolute paths and `.` or `..` segments are rejected. Settings are written to a unique temporary file and atomically replaced. This file contains no player progress. Save slot format and checksum remain the `ASTRO_SAVE 2` / `ASTRO_PUZZLE_SNAPSHOT 1` formats above.

## Campaign transfer recovery journal v1 (06.40)

The copy adapter creates `<target>/.astro-campaign-transfer.pending` exclusively before writing target slots. The bounded UTF-8/ASCII line record is capped at 4096 bytes and lists 1–20 expected slot artifacts. It records only campaign ID and artifact metadata; it does not contain save payloads or arbitrary paths. The file is flushed before copy begins.

```text
ASTRO_CAMPAIGN_TRANSFER 1
CAMPAIGN <safe-campaign-id>
COUNT <1..20>
ITEM <0..9> <P|B> <byte-count> <fnv1a-64-hex>
...
END
```

`P` identifies the primary slot file, `B` its `.bak` file. Duplicate slot/kind pairs, unsafe IDs, unsupported versions, extra rows, malformed values and oversized journals are rejected. FNV-1a is a stale/interruption diagnostic fingerprint, not cryptographic integrity or proof of authorship. Preview reports whether each expected target path is missing, complete, partial, changed or unsafe. It never repairs or removes slot files. Copy/resume hold a journal lifecycle lock through the operation. An explicitly confirmed acknowledgement deletes only an unchanged valid journal after rechecking the displayed artifacts while holding that lock. Windows uses `LockFileEx` and deletes the opened file object; POSIX uses advisory `flock`, directory-entry identity comparison, and `unlinkat`. POSIX advisory locks coordinate only cooperating processes. Incomplete slots remain and continue to conflict with a new copy. A torn/invalid journal requires manual review and is not removed by the acknowledgement API. The flushed file supports detection after a process exit; parent-directory durability under sudden power loss is not guaranteed. See [D110](DECISIONS.md#d110--cross-platform-campaign-journal-lifecycle-lock-0647).

## Puzzle level v1 (06.18)

`<project>/Puzzle/Levels/<safe-ID>.astro-puzzle-level` is project-authored content, separate from `.astro/puzzle-save.settings` and user progress slots.

```text
ASTRO_PUZZLE_LEVEL 1
ID "<1-64 ASCII letters/digits/hyphens/underscores>"
NAME "<1-256 valid UTF-8 bytes>"
BOARD <width> <height> <tile-kinds> <seed-u32> <target-score-u64>
TILES <cell-count> <tile-id-0> ... <tile-id-n>
END
```

Files are limited to 64 KiB, dimensions to 3–64 per side and at most 4096 cells, tile kinds to 3–32, target score must be positive, and every tile ID must be less than tile-kinds. Unknown versions, malformed/trailing data, invalid UTF-8 and unsafe level IDs fail without returning a partial definition. The resolved `Puzzle/Levels` path must remain inside the project, including after symlink/junction resolution. Writes use a sibling temporary and atomic replacement. v1 has no migration path yet. `PuzzleGame::create(const PuzzleLevelDefinition&)` starts from the exact tile array; the existing seeded board generator remains unchanged.

Публикация использует уникальный файл рядом со slot, flush перед атомарной заменой. Валидная прежняя версия сохраняется рядом как `slot-<index>.astro-save.bak`. Load при ошибке checksum/структуры основного файла может использовать валидную backup-копию; fallback отражается в результате и не переписывает основной файл. Временные файлы после падения процесса не считаются слотами.

Checkpoint definition, active checkpoint ID и respawn policy в 05.5 остаются runtime-only и не имеют отдельной Scene/file записи. Для режима `SaveSelectedProfile` checkpoint повторно использует формат и совместимость `.astro-save`; повторное сохранение выбранного слота создаёт обычный `.bak`.
## Authored Puzzle progress (06.19)

The underlying slot remains `ASTRO_SAVE 2` containing the selected Puzzle snapshot field `puzzle.snapshot` and the selected string custom field `puzzle.level.id`; no file-format version changes. The profile fingerprint binds the selected keys and types, while the adapter separately checks the stored level-ID value against the requested active level during load. Editor stores those slots at `<SDL user-data>/<project-subdirectory>/PuzzleLevels/<ID>/slot-<n>.astro-save`. The level ID is restricted to 1–64 ASCII letters, digits, `-` and `_`. The built-in sample keeps its prior profile/path. No migration or automatic file movement is performed.
## Puzzle level catalog operations (06.20)

The catalog is derived from `<project>/Puzzle/Levels`; it is not a separate manifest and introduces no serialized project setting. Enumeration considers only `.astro-puzzle-level` entries, returns a bounded sorted list, and attaches a diagnostic to malformed, unsafe, or non-regular entries. New/duplicate destinations use create-only publication and fail if the ID is already present. Rename publishes a new v1 file and then removes the old ID path after a source consistency check. Existing slots under the old `PuzzleLevels/<ID>` user-data folder stay there; no slot copy, deletion, or migration occurs.

## Puzzle campaign (06.22, parser and project-file API)

`Puzzle/Campaigns/<ID>.astro-puzzle-campaign` uses `ASTRO_PUZZLE_CAMPAIGN 1`, an explicit ordered level list, a start ID, and score-threshold/default transitions. `PuzzleCampaignFile::read/write` additionally verify each referenced authored level file and enforce safe project-contained paths; writes use a temporary sibling and atomic replacement. Strict grammar, bounds, validation, and limitations are documented in [PUZZLE_CAMPAIGNS.md](PUZZLE_CAMPAIGNS.md). The parser, serializer, structural validator, and project-file API are implemented and covered by `runtime.puzzle.campaign.integration`; Editor authoring, runtime transition selection, migration, and reference-safe level rename remain unimplemented.
# Scene format v12 — DirectionalLight3D (07.2bw)

Scene v12 adds an optional component record:

```text
COMPONENT <component-id> <entity-id> "astro.DirectionalLight3D" 1 <enabled:0|1> <linear-r> <linear-g> <linear-b> <intensity>
```

The three color values must be finite and in `[0,1]`; intensity must be finite and in `[0,64]`. v1–v11 remain readable. Migration is in-memory only until the user explicitly saves; the current writer emits v13. Unknown component types and malformed records continue to fail rather than being discarded.

## Scene format v13 — PointLight3D (07.2cx)

Scene v13 adds an optional point-light record:

```text
COMPONENT <component-id> <entity-id> "astro.PointLight3D" 1 <enabled:0|1> <linear-r> <linear-g> <linear-b> <intensity> <range>
```

Color channels must be finite in `[0,1]`, intensity finite in `[0,64]`, and range finite in `[0.1,10000]` scene units. v1–v12 remain readable; loading v12 never invents a point light or rewrites the source file. An explicit save writes v13. Unknown component types and malformed/unsupported component records fail with diagnostics.
# Material color semantics (07.2cc)

No `.astro-material` schema or serialized color-space field was added. Numeric base-color factors are linear-light values. For glTF imports, base-color texture RGB is sRGB encoded and alpha is linear; `COLOR_0` is a linear multiplier. BGFX converts source RGB, applies factors and creates mips before upload, so its uploaded BGRA8 bytes are linear. See the [Khronos glTF 2.0 material specification](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#reference-material-pbrmetallicroughness). Renderer-specific transfer steps are implementation details and must not change saved material values.

Windows D3D11 BGFX shader paths encode RGB for ordinary BGRA8 outputs; screenshot smoke covers opaque/unlit/lit texture and vertex-color samples. Alpha remains linear. Fixed-function alpha blending into these ordinary targets is not yet established as linear-light correct; this limitation does not change the file format.

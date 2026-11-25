# Asset Registry, импорт изображений и script references (03.2, 04.8)

В 07.2av зарегистрированная glTF-модель визуализируется в отдельном Project viewport через временную Scene и существующие MeshResolver/SceneViewTarget. Импорт ограничен 128 primitives и 64 MiB vertex/index geometry; renderer сохраняет свои per-mesh и material-texture cache budgets. Отдельные BGFX views/framebuffer и GPU cache очищаются при смене AssetId или project root. Это не build/export и не редактирование source: импорт может создать производный файл в Library, Assets остаются неизменны. Подробности: [3D rendering](3D_RENDERING.md#project-model-preview-072av), [TESTING](TESTING.md#072av-3d-preview-модели-в-project).

В 07.2at Project получает начальный read-only Asset Browser из четырёх текущих Registry maps: image, model, material и script. Каталог показывает стабильные AssetId и source-relative paths, не перечисляет незарегистрированные файлы. В 07.2au выбранное изображение показывается из валидированного Library cache, а зарегистрированная модель анализируется glTF importer для показа геометрической сводки. Импорт может создавать/переиспользовать производный кэш; исходники Assets остаются без изменений. Это не 3D viewport и не preview material/script. UTF-8 substring filter и фильтр типа описаны в [EDITOR_UX.md](EDITOR_UX.md#asset-browser-072at); результаты — в [отчёте 07.2au](reports/07.2au-asset-previews.md).

Game View renderer diagnostics (07.2as) can navigate to a registered model or project material by typed AssetId. The Editor validates the ID against the active registry and matching project root, then displays only its type, ID and registry-relative source path. This diagnostic selection does not open or modify the source, does not generate cache data, and is not a full Asset Browser. See [the stage report](reports/07.2as-game-view-registered-asset-navigation.md).

```cpp
#include "astro/assets/AssetRegistry.hpp"

const auto opened = astro::assets::AssetRegistry::open(projectRoot);
if (!opened) { report(opened.error().message); return; }
auto assets = opened.value();
const auto imported = assets.importImage(
    std::filesystem::path(std::u8string(u8"Sprites/player.png")));
if (!imported) { report(imported.error().message); return; }
const auto reference = imported.value(); // AssetId, dimensions, format, SHA-256, source-relative path
const auto cachePath = assets.cachedPath(reference.id);
```

`projectRoot` must be an existing directory. Opening creates `Assets/`, `Library/Assets/` and `.astro/` inside the project if absent. If these paths resolve through symlinks outside the project root, opening fails. The cache and metadata directory must stay under the project root.

## Поддерживаемые форматы и проверки

Принимаются `.png`, `.jpg`, `.jpeg` (расширение без учёта ASCII-регистра). Размер одного файла ограничен 256 MiB, размеры каждой стороны — 65535, суммарный пиксельный бюджет — 268435456. PNG проходит проверку signature, структуры chunks, CRC chunks, IHDR, допустимых color/depth значений, IDAT/IEND и размера. JPEG проверяет SOI/EOI, границы сегментов и наличие SOF с размерами.

Импортёр не декодирует пиксели: он хранит источник как непрозрачный encoded-файл. Для PNG не распаковывается поток IDAT и не проверяется семантическая корректность декодированного изображения; для JPEG не декодируются таблицы/entropy stream. Проверка формата поэтому не является защитой от всех malformed или специально сформированных изображений, и декодирование позднее должно выполняться контролируемым renderer/import adapter.

## IDs, индекс и reimport

Индекс `.astro/assets.index` версии 2 хранит один монотонный AssetId namespace для image и C# script assets. Reader принимает image-only v1; простое открытие не переписывает файл, а явная новая регистрация/reimport публикует v2 атомарно. Image record содержит путь `Assets/`, формат, размеры, размер источника и SHA-256. Script record содержит UTF-8 source path в `Scripts/`, managed type, output assembly/runtimeconfig paths под `Library/Scripts/` и SHA-256 исходника. Индекс ограничен 16 MiB/100000 всех записей/4096 байт на путь. Повреждённый индекс не исправляется молча. Одновременное многопроцессное редактирование индекса не поддерживается.

Повторный импорт неизменившегося image source возвращает прежний ID. Если байты изменились, `importImage` сообщает об этом и просит вызвать `reimport(id)`. Reimport повторно валидирует источник, обновляет metadata/hash, сохраняет AssetId и атомарно публикует индекс. Script registration требует уже собранные DLL/runtimeconfig и вычисляет source hash; `resolveScript(id)` проверяет неизменность `.cs`, ограничения путей и наличие build outputs. После правки файла script надо пересобрать и зарегистрировать повторно с тем же source path/type mapping. Registry не компилирует скрипты, не проверяет тип без hostfxr и не является sandbox. Source-файлы и output assemblies инструмент не переписывает. Неполные/отсутствующие refs остаются доступными для диагностики разрешением по AssetId.

В 04.10 C# builder публикует хешированный `Library/Scripts/asset-bindings.state` из manifest's explicit `scriptBindings`. `AssetRegistry::inspectScriptBuild()` проверяет manifest, полный список `.cs`, DLL и runtimeconfig, затем сообщает freshness и расхождение с текущими записями индекса. `registerScriptsFromBuild()` — отдельное действие: все bindings проверяются до единственной публикации индекса, а повторная регистрация source сохраняет AssetId. Окно Project показывает результаты и позволяет обновить статус/выполнить синхронизацию. Это не CLR type discovery: сам managed type подтверждается только явным вызовом host loader. Формат handoff описан в [FILE_FORMATS.md](FILE_FORMATS.md), детали — [отчёт 04.10](reports/04.10-csharp-build-registry-diagnostics.md).

## Content-addressed cache

Кэш находится в `Library/Assets/` и игнорируется Git. Имя производного файла: `<sha256>.v1.png` или `<sha256>.v1.jpg`. Сейчас «производная» копия байт-в-байт равна encoded source: изображения не перекодируются и не оптимизируются. Одинаковые байты одного формата используют общий cache entry. Изменённые/повреждённые cache-файлы восстанавливаются из source при import/reimport; старые версии кэша не удаляются автоматически.

`.astro/assets.index` записывается во временный файл, созданный эксклюзивно, и атомарно заменяется. На Windows применяются `MoveFileExW` с replace/write-through; переносимость и свойства прочих файловых систем пока не проверены. `cachedPath(id)` возвращает путь только к текущему обычному файлу внутри кэша, иначе пустой путь.

На 03.3 `astro.SpriteRenderer` хранит AssetId и visible; `entity.set_sprite_renderer` поддерживает транзакционный Undo/Redo, а Astro Scene v3 сохраняет ссылку. Windows Editor предлагает ручной Assets-relative PNG/JPEG import, preview выбранного спрайта через WIC/BGFX и показывает missing source/AssetId. WIC ограничивает preview по размерам и пиксельному бюджету; importer metadata validation и codec decoding — отдельные уровни проверки. 07.2at добавляет реестровый read-only каталог с фильтрами, но не directory browser/drag-and-drop. Нет runtime/spatial renderer, platform adapter кроме Windows, importer plugins, watch service, automatic reimport или очистки старого кэша.

С 07.2h `runtime::decodeWicImage` является общей Windows WIC decode-функцией для зарегистрированного кэшированного изображения: BGRA8, до 16 384 × 16 384 и не более 16 Mi pixels. `SDLImageTextureCache` использует тот же декодер. Вызывающий код должен передавать проверенный `AssetRegistry::cachedPath`, а не произвольный путь. PNG metadata validation при импорте всё ещё не заменяет codec decode; на других платформах нужен другой decoder adapter.

В 07.2i 3D smoke дополнительно создаёт временную линейную BGRA8 копию base-color texels: RGB проходит IEC sRGB decode и умножается на linear material factor, alpha умножается напрямую. Source PNG и `Library/Assets` cache остаются неизменными. Эта подготовка специфична для тестового BGFX sample; production texture cache и GPU sRGB texture view не добавлены.

## Граф зависимостей и пакет 03.5

`AssetDependencyGraph` хранит явные связи AssetId -> AssetId, запрещает нулевые ID и циклы, вычисляет отсортированное транзитивное замыкание и сохраняется в `.astro/assets.dependencies` (`ASTRO_DEPENDENCIES 1`). Сцены/prefabs не анализируются автоматически: граф заполняет вызывающий код.

`buildAssetPackage(registry, graph, roots, platform)` включает только корни и их зависимости, сверяет зарегистрированный ID, исходник и cache по размеру/SHA-256 и записывает детерминированный пакет в `Library/Derived/<platform>/<sha256>.v1.astro-pack`. Совпадающий пакет переиспользуется, повреждённый артефакт атомарно заменяется. PNG/JPEG payload остаётся исходными encoded байтами.

Метки `windows`, `macos`, `android`, `ios` — только имена профилей. Нет перекодирования, оптимизации, платформенных codec/runtime adapters, сборки или проверки других платформ. Это cache/package prototype, а не готовый Export.

## Optional glTF geometry import (07.1b)

The separate `astro_gltf` prototype imports static triangle meshes through `astro::render3d::GltfImporter::importFile`. 07.1c adds explicit `GltfImporter::importAndRegister(registry, path)`, which registers a stable model AssetId after successful import. Asset index v3 stores the top-level model source size/hash and importer content digest. 07.1d adds `resolveMeshRenderer(registry, component)`: it rechecks the registered model, imports/loads current source geometry, compares the importer digest (thereby detecting changed external buffers), validates mesh and primitive indices, and returns the selected backend-neutral geometry. 07.1e consumes this result in a standalone optional BGFX visual smoke. When source bytes or external-buffer content changed, update the record with explicit `importAndRegister` before resolving again. Per-buffer dependency records and runtime package inclusion are not yet implemented. Original model and buffer bytes remain read-only. The BGFX test operates on an isolated fixture and does not modify project resources. See [07.1b importer details](3D_RENDERING.md#bounded-gltfglb-geometry-import-071b), [07.1c integration details](3D_RENDERING.md#scene-model-asset-reference-071c), [07.1d resolver details](3D_RENDERING.md#scene-mesh-resolution-071d), [07.1e preview details](3D_RENDERING.md#standalone-scene-mesh-bgfx-preview-071e), and [07.1e report](reports/07.1e-scene-bgfx-preview.md).
# Materials (07.2b)

Reusable material data is stored in versioned `Assets/Materials/*.astro-material` files and registered in the shared `.astro/assets.index` as Asset index v4. Use `AssetRegistry::registerMaterial` to validate and explicitly register a file; repeated registration at the same project-relative path retains the material AssetId. `resolveMaterial` verifies the recorded source size/SHA-256, parses the file and checks each typed texture reference names a registered image AssetId. The material file is read-only from the registry's perspective; edits are caller-owned and must be followed by explicit registration to update the index hash.

The parser and registry do not decode or import images, verify the current bytes of each referenced image during material resolve, create GPU resources, or connect material data to a Scene or renderer. See [material format](FILE_FORMATS.md#material-asset-v1-072b), [07.2b report](reports/07.2b-material-asset-and-registry.md), and [D134](DECISIONS.md#d134--versioned-material-asset-and-registry-072b).

## glTF material metadata (07.2c)

`GltfImporter::importFile` now exposes supported source material values and texture mappings through `GltfImportResult::materials`, with `ImportedGltfMesh::primitiveMaterialIndices` preserving each source primitive's optional material index. `images` and `textures` preserve their glTF source indices and image references. This mapping does not register image AssetIds or write `.astro-material` files. Local image paths must resolve safely under `Assets/`; their source bytes are read only for bounded hash/freshness and are never rewritten or decoded. Missing files stay visible as diagnostics, remote URIs are not fetched, and path traversal is rejected. The result/cache-key and exact unsupported cases are documented in [FILE_FORMATS.md](FILE_FORMATS.md#gltf-material-import-metadata-072c), [3D_RENDERING.md](3D_RENDERING.md#bounded-gltf-material-metadata-072c), and [the 07.2c report](reports/07.2c-gltf-material-metadata.md).

# Asset Registry, импорт изображений и script references (03.2, 04.8)

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

На 03.3 `astro.SpriteRenderer` хранит AssetId и visible; `entity.set_sprite_renderer` поддерживает транзакционный Undo/Redo, а Astro Scene v3 сохраняет ссылку. Windows Editor предлагает ручной Assets-relative PNG/JPEG import, preview выбранного спрайта через WIC/BGFX и показывает missing source/AssetId. WIC ограничивает preview по размерам и пиксельному бюджету; importer metadata validation и codec decoding — отдельные уровни проверки. Нет runtime/spatial renderer, platform adapter кроме Windows, asset browser drag-and-drop, dependency graph, importer plugins, watch service, automatic reimport или очистки старого кэша.

## Граф зависимостей и пакет 03.5

`AssetDependencyGraph` хранит явные связи AssetId -> AssetId, запрещает нулевые ID и циклы, вычисляет отсортированное транзитивное замыкание и сохраняется в `.astro/assets.dependencies` (`ASTRO_DEPENDENCIES 1`). Сцены/prefabs не анализируются автоматически: граф заполняет вызывающий код.

`buildAssetPackage(registry, graph, roots, platform)` включает только корни и их зависимости, сверяет зарегистрированный ID, исходник и cache по размеру/SHA-256 и записывает детерминированный пакет в `Library/Derived/<platform>/<sha256>.v1.astro-pack`. Совпадающий пакет переиспользуется, повреждённый артефакт атомарно заменяется. PNG/JPEG payload остаётся исходными encoded байтами.

Метки `windows`, `macos`, `android`, `ios` — только имена профилей. Нет перекодирования, оптимизации, платформенных codec/runtime adapters, сборки или проверки других платформ. Это cache/package prototype, а не готовый Export.

## Optional glTF geometry import (07.1b)

The separate `astro_gltf` prototype imports static triangle meshes through `astro::render3d::GltfImporter::importFile`. 07.1c adds explicit `GltfImporter::importAndRegister(registry, path)`, which registers a stable model AssetId after successful import. Asset index v3 stores the top-level model source size/hash and importer content digest. 07.1d adds `resolveMeshRenderer(registry, component)`: it rechecks the registered model, imports/loads current source geometry, compares the importer digest (thereby detecting changed external buffers), validates mesh and primitive indices, and returns the selected backend-neutral geometry. 07.1e consumes this result in a standalone optional BGFX visual smoke. When source bytes or external-buffer content changed, update the record with explicit `importAndRegister` before resolving again. Per-buffer dependency records and runtime package inclusion are not yet implemented. Original model and buffer bytes remain read-only. The BGFX test operates on an isolated fixture and does not modify project resources. See [07.1b importer details](3D_RENDERING.md#bounded-gltfglb-geometry-import-071b), [07.1c integration details](3D_RENDERING.md#scene-model-asset-reference-071c), [07.1d resolver details](3D_RENDERING.md#scene-mesh-resolution-071d), [07.1e preview details](3D_RENDERING.md#standalone-scene-mesh-bgfx-preview-071e), and [07.1e report](reports/07.1e-scene-bgfx-preview.md).

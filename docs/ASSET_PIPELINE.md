# Asset Registry и импорт изображений (03.2)

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

Индекс `.astro/assets.index` версии 1 хранит монотонный numeric AssetId, UTF-8 относительный путь внутри `Assets/`, формат, ширину, высоту, размер источника и SHA-256. Registry валидирует индекс до использования, поддерживает LF/CRLF, ограничивает его 16 MiB/100000 записей/4096 байт на путь и не исправляет повреждённый индекс молча. ID переживает закрытие/повторное открытие registry и reimport. Одновременное многопроцессное редактирование индекса не поддерживается.

Повторный импорт неизменившегося источника возвращает прежний ID. Если байты изменились, `importImage` сообщает об этом и просит вызвать `reimport(id)`. Reimport повторно валидирует источник, обновляет metadata и hash, сохраняет AssetId и атомарно публикует обновлённый index. Source никогда не записывается. `missingAssets()` по запросу находит отсутствующие/вышедшие за project root source paths; индекс при этом остаётся доступен для диагностики. Интеграция диагностики в панели редактора относится к последующему подэтапу.

## Content-addressed cache

Кэш находится в `Library/Assets/` и игнорируется Git. Имя производного файла: `<sha256>.v1.png` или `<sha256>.v1.jpg`. Сейчас «производная» копия байт-в-байт равна encoded source: изображения не перекодируются и не оптимизируются. Одинаковые байты одного формата используют общий cache entry. Изменённые/повреждённые cache-файлы восстанавливаются из source при import/reimport; старые версии кэша не удаляются автоматически.

`.astro/assets.index` записывается во временный файл, созданный эксклюзивно, и атомарно заменяется. На Windows применяются `MoveFileExW` с replace/write-through; переносимость и свойства прочих файловых систем пока не проверены. `cachedPath(id)` возвращает путь только к текущему обычному файлу внутри кэша, иначе пустой путь.

На 03.3 `astro.SpriteRenderer` хранит AssetId и visible; `entity.set_sprite_renderer` поддерживает транзакционный Undo/Redo, а Astro Scene v3 сохраняет ссылку. Windows Editor предлагает ручной Assets-relative PNG/JPEG import, preview выбранного спрайта через WIC/BGFX и показывает missing source/AssetId. WIC ограничивает preview по размерам и пиксельному бюджету; importer metadata validation и codec decoding — отдельные уровни проверки. Нет runtime/spatial renderer, platform adapter кроме Windows, asset browser drag-and-drop, dependency graph, importer plugins, watch service, automatic reimport или очистки старого кэша.

## Граф зависимостей и пакет 03.5

`AssetDependencyGraph` хранит явные связи AssetId -> AssetId, запрещает нулевые ID и циклы, вычисляет отсортированное транзитивное замыкание и сохраняется в `.astro/assets.dependencies` (`ASTRO_DEPENDENCIES 1`). Сцены/prefabs не анализируются автоматически: граф заполняет вызывающий код.

`buildAssetPackage(registry, graph, roots, platform)` включает только корни и их зависимости, сверяет зарегистрированный ID, исходник и cache по размеру/SHA-256 и записывает детерминированный пакет в `Library/Derived/<platform>/<sha256>.v1.astro-pack`. Совпадающий пакет переиспользуется, повреждённый артефакт атомарно заменяется. PNG/JPEG payload остаётся исходными encoded байтами.

Метки `windows`, `macos`, `android`, `ios` — только имена профилей. Нет перекодирования, оптимизации, платформенных codec/runtime adapters, сборки или проверки других платформ. Это cache/package prototype, а не готовый Export.

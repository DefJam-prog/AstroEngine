# Формат файлов сцен AstroEngine

## Astro Scene v4

`*.astro-scene` — UTF-8 текстовый формат редакторской Scene/Entity модели.

```text
ASTRO_SCENE 4
SCENE <scene-id> "<quoted scene name>"
ENTITY <entity-id> <parent-id-or-0> "<quoted entity name>"
COMPONENT <component-id> <entity-id> "astro.Transform" 1 <position xyz> <rotation-degrees xyz> <scale xyz>
COMPONENT <component-id> <entity-id> "astro.SpriteRenderer" 1 <asset-id> <visible-0-or-1>
PREFAB_INSTANCE <root-entity-id> <prefab-id> <source-link-count> <override-count>
PREFAB_LINK <root-entity-id> "<source-key>" <entity-id>
PREFAB_OVERRIDE <root-entity-id> "<source-key>" <property-kind> <typed-value>
END
```

Имена и type ID записываются средствами `std::quoted`; числа разбираются с classic locale. Для каждой сущности требуется ровно одна запись Transform версии 1. Scene ID, Entity ID и Component ID ненулевые и не могут достигать максимума своего unsigned типа. Entity ID и Component ID не повторяются внутри сцены; Component ID глобально уникален для этой сцены. Родитель должен существовать, циклы запрещены. Transform числа должны быть конечными. Position и rotation по умолчанию нулевые, scale — (1,1,1). Rotation — локальные углы Эйлера в градусах; композиция и мировые координаты не реализованы.

Загрузка ограничивает файл 16 MiB и 100000 сущностей. Каждая сущность содержит ровно один Transform; SpriteRenderer необязателен. Prefab instance metadata включает root, PrefabId, карту стабильных source keys к EntityId и типизированные overrides (`Name`, целый `Transform`, наличие/AssetId/visibility `SpriteRenderer`). Counts записей проверяются. Неизвестный тип компонента/override, версия, повреждённый UTF-8, неверная структура, повторные ID, отсутствующий владелец/родитель/source key и лишние записи дают ошибку; данные не отбрасываются молча. Полная проверка завершается до публикации загруженной модели.

Сохранение сериализует в уникальный временный файл рядом с назначением, затем атомарно заменяет его (Windows `MoveFileExW` с replace/write-through). Это не гарантирует восстановление после аварийного завершения до замены, резервные копии, checksum или одинаковую долговечность на всех файловых системах.

## Миграция Astro Scene v1/v2/v3 → v4

Загрузчик читает v1 без компонентов, v2 с Transform и v3 с Transform/SpriteRenderer. Для v1 создаются Transform версии 1 с новыми Component ID и значениями по умолчанию; старые Component ID и Transform v2/v3 сохраняются. Сохранение старых версий записывает v4. Старый файл не переписывается автоматически при открытии.

## Границы

На этапе 03.3 реализованы Transform и SpriteRenderer; в 03.4 — instance links и overrides. Windows Editor показывает WIC preview, но Scene не является runtime drawing. Нет геометрии/порядка слоёв, материалов, анимации и межплатформенного декодера. Формат Scene не является форматом прогресса игры или Save System.

## Prefab asset v1

`<project>/Prefabs/<prefab-id>.astro-prefab` — UTF-8 файл с `ASTRO_PREFAB 1`, записью PREFAB (числовой ID и quoted name), NODE records и END. NODE хранит stable local ID, parent local ID (0 только у корня), имя, Transform, необязательную ссылку SpriteRenderer (AssetId/visible) и необязательный nested PrefabId. Требуется ровно один root, уникальные local IDs, валидные родители/Transform/name; лимиты — 16 MiB, 100000 node records и 256 уровней локальной иерархии. Файл пишется через уникальный временный файл и атомарную замену.

Вложенная prefab ссылка разворачивается как дочернее дерево под узлом-владельцем. Цикл graph и глубина свыше 64 отклоняются до изменения Scene. Source key — полный путь числовых PrefabId/localId от корневого ассета; обновление сопоставляет эти ключи и сохраняет EntityId. Instance source map и overrides записываются в Scene v4. Миграция prefab-файла пока не нужна (поддерживается только v1); Editor UI для создания/обновления и конфликтный merge отсутствуют.

## Asset index v1

`<project>/.astro/assets.index` — UTF-8 line-based registry manifest:

```text
ASTRO_ASSETS 1
NEXT <next-asset-id>
ASSET <asset-id> "<Assets-relative UTF-8 path>" PNG|JPEG <width> <height> <source-size> <sha256-hex>
END
```

Пути и quoted strings сериализуются через `std::quoted`. Значения ID положительные; `NEXT` больше всех занятых ID. Дубликаты ID и source path, неверный hash/размер/путь/format, неизвестная версия, лишние строки и повреждённый UTF-8 отклоняются. Индекс ограничен 16 MiB, 100000 asset entries и 4096 UTF-8 байт на source path. Сохранение использует временный файл и атомарную замену. Повреждённый индекс остаётся нетронутым и возвращает ошибку.

Кэшированный файл находится в `Library/Assets/<sha256>.v1.<png|jpg>`. Сейчас он побайтно равен encoded source и восстанавливается через импорт; старые хэш-версии не удаляются. `Library/` добавлен в `.gitignore`; `.astro/assets.index` сохраняется в проекте.

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

Публикация использует уникальный файл рядом со slot, flush перед атомарной заменой. Валидная прежняя версия сохраняется рядом как `slot-<index>.astro-save.bak`. Load при ошибке checksum/структуры основного файла может использовать валидную backup-копию; fallback отражается в результате и не переписывает основной файл. Временные файлы после падения процесса не считаются слотами.

# Формат файлов сцен AstroEngine

## Astro Scene v3

`*.astro-scene` — UTF-8 текстовый формат редакторской Scene/Entity модели.

```text
ASTRO_SCENE 3
SCENE <scene-id> "<quoted scene name>"
ENTITY <entity-id> <parent-id-or-0> "<quoted entity name>"
COMPONENT <component-id> <entity-id> "astro.Transform" 1 <position xyz> <rotation-degrees xyz> <scale xyz>
COMPONENT <component-id> <entity-id> "astro.SpriteRenderer" 1 <asset-id> <visible-0-or-1>
END
```

Имена и type ID записываются средствами `std::quoted`; числа разбираются с classic locale. Для каждой сущности требуется ровно одна запись Transform версии 1. Scene ID, Entity ID и Component ID ненулевые и не могут достигать максимума своего unsigned типа. Entity ID и Component ID не повторяются внутри сцены; Component ID глобально уникален для этой сцены. Родитель должен существовать, циклы запрещены. Transform числа должны быть конечными. Position и rotation по умолчанию нулевые, scale — (1,1,1). Rotation — локальные углы Эйлера в градусах; композиция и мировые координаты не реализованы.

Загрузка ограничивает файл 16 MiB и 100000 сущностей. Каждая сущность содержит ровно один Transform; SpriteRenderer необязателен и ссылается на положительный AssetId, а `visible` равен 0 или 1. Неизвестный тип компонента, версия файла или компонента, повреждённый UTF-8, неверная структура, повторные ID, отсутствующий владелец/родитель, неполный компонент и лишние записи дают ошибку; неизвестное содержимое не пропускается молча. Полная проверка завершается до публикации загруженной модели.

Сохранение сериализует в уникальный временный файл рядом с назначением, затем атомарно заменяет его (Windows `MoveFileExW` с replace/write-through). Это не гарантирует восстановление после аварийного завершения до замены, резервные копии, checksum или одинаковую долговечность на всех файловых системах.

## Миграция Astro Scene v1/v2 → v3

Загрузчик читает v1 без компонентов и v2 с Transform. Для v1 создаются Transform версии 1 с новыми Component ID и значениями по умолчанию; в v2 существующие Component ID и данные Transform сохраняются. Сохранение обеих версий записывает v3. Старый файл не переписывается автоматически при открытии.

## Границы

На этапе 03.3 реализованы Transform и базовый SpriteRenderer с AssetId и видимостью. В редакторе Windows PNG/JPEG декодируются системным WIC и показываются как preview выбранной сущности; это пока не spatial renderer и не runtime drawing. Нет геометрии/порядка слоёв, материалов, цветов/UV, анимации, собственного межплатформенного декодера или сохранения исходных байтов в Scene. Отсутствующий AssetId/source показывается в редакторских Diagnostics/Inspector. Формат Scene не является форматом прогресса игры или Save System.

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

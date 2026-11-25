# Предварительный стек и технические решения

Статус: предложение пользователя, подлежащее проверке в техническом прототипе 00.3. Стек не утверждён окончательно. Версии зависимостей пока не выбраны.

Результат 00.3: базовая комбинация SDL3 + bgfx/D3D11 + Dear ImGui docking фактически собрана и прошла smoke на Windows x64/MSVC. Проверенные revisions и ограничения зафиксированы в [отчёте](reports/00.3.md). Это подтверждение прототипа, стек остаётся предварительным.

## Базовое предложение

| Область | Предложение |
| --- | --- |
| Язык | C++20 |
| Сборка и тесты | CMake + CTest |
| Контроль версий | Git |
| Окна и ввод | SDL3 |
| Рендеринг | bgfx |
| Интерфейс редактора | Dear ImGui с docking |
| Конфигурация и ранняя сериализация | JSON; конкретная библиотека не выбрана |

Для каждой зависимости обязательны проверка совместимости, лицензии и фактической сборки. Неподтверждённые версии не фиксируются.

## Границы подсистем

Предусмотреть адаптеры рендеринга, физики, звука и импорта, чтобы замена библиотек не требовала переписывания ядра. Это требование к будущему проектированию, а не описание уже реализованных интерфейсов.

Необязательные подсистемы не запускаются и не включаются в сборку без необходимости. Editor, Runtime и Build/Export разделяются; готовая игра не включает редактор. См. [продуктовые требования](PRODUCT_REQUIREMENTS.md).

## Кандидаты для последующей оценки

| Область | Кандидаты |
| --- | --- |
| Компонентная модель | EnTT или собственная небольшая компонентная модель |
| Физика | Jolt / Box2D для соответствующих задач |
| Звук | miniaudio |
| Формат 3D-ресурсов | glTF 2.0 |
| Форматы изображений | PNG / JPEG |

Это кандидаты, а не обязательные зависимости. Выбор формата не означает выбор библиотеки его обработки.

При оценке учитывать лицензии, размеры сборок, возможности платформ, обслуживание и CI. Не разрабатывать сложный физический движок или универсальный импортёр с нуля без обоснованной необходимости.

## Что подтвердить в прототипе 00.3

- Конкретные версии или ревизии, совместимость выбранных компонентов и фактическую сборку.
- Лицензии и требования к распространению, включая транзитивные зависимости.
- Поддерживаемые платформы, ограничения интеграции и размеры полученных сборок.
- Воспроизводимость, обслуживание и возможность проверки в CI.
- Windows-пути, Unicode, UTF-8 и чистую сборку; условия работы без сети, включая предварительную доступность зависимостей.

Результаты подтверждаются командами, окружением и наблюдаемыми результатами. До этих проверок совместимость, лицензии и сборка считаются непроверенными. Прототип 00.3 начинается только по отдельному указанию пользователя.

## Оценка кандидатов для 3D (07.1 assessment)

В 07.1 проведена только предварительная оценка; зависимости не загружались, не добавлялись в CMake и не утверждаются окончательно.

| Назначение | Кандидат и наблюдение | Статус |
| --- | --- | --- |
| Render backend | Уже выбранный предварительный bgfx документирует D3D11/D3D12, Metal, OpenGL, OpenGL ES, Vulkan и другие backend; лицензия BSD-2-Clause. В проекте фактически подтверждены Windows/MSVC/D3D11 и 2D draw path. | Оставить как адаптер-кандидат для 3D spike; upstream platform list не заменяет сборку/визуальный тест AstroEngine на этих платформах. |
| glTF/GLB parser | cgltf — C99 single-header parser; просмотренный upstream header сообщает версию 1.15 и MIT. Он не загружает внешние buffers автоматически; их чтение остаётся обязанностью вызывающей стороны. | Кандидат только для optional importer target после path/size/URI security tests. Не vendored и версия не закреплена. |
| 3D physics | Jolt заявляет MIT, C++17, отсутствие внешних библиотек кроме STL, MSVC 2022+/Clang 16+/GCC 12+ и platform support для Windows, Linux, macOS, Android и iOS. | Наиболее подходящий кандидат для отдельного physics adapter; не добавлен и не проверен в нашем CMake/CI. Box2D является 2D-кандидатом и не закрывает 3D physics. |

После 07.2b reusable material data хранится в backend-neutral `ASTRO_MATERIAL 1` файле; AssetRegistry v4 назначает ему стабильный AssetId и держит image bindings как AssetIds. BGFX handles и shader variants остаются runtime cache и не сериализуются в Scene/asset data. Никаких новых библиотек не добавлено. Наличие D3D11 cube binaries в локальном кэше не подтверждает PBR или другие backend. Перед PBR нужны importer normals/UV/material slots, texture decode/upload, проверяемая reference-scene и отдельное ограничение shader features; Inspector, lights и shadows ещё отсутствуют. Подробности: [07.2a assessment](reports/07.2a-material-authoring-contract.md), [07.2b report](reports/07.2b-material-asset-and-registry.md), [D133](DECISIONS.md#d133--material-authoring-contract-before-implementation-072a), [D134](DECISIONS.md#d134--versioned-material-asset-and-registry-072b).

07.2c uses fastgltf and the simdjson already staged with it to extract a bounded core material subset and preserve source texture/image indices. This does not add a dependency. External image contents are read only for size/hash freshness (64 MiB per image, 128 MiB total); no codec or GPU texture path is added. Details and diagnostics are in [07.2c report](reports/07.2c-gltf-material-metadata.md) and [D135](DECISIONS.md#d135--bounded-gltf-material-metadata-import-072c).

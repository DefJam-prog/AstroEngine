# Статус проекта

Обновлено: 2026-11-13.

| Область | Состояние |
| --- | --- |
| Продуктовые требования P01–P14 | Записаны в PRODUCT_REQUIREMENTS.md; реализация отсутствует |
| Предварительный стек | Записан в TECH_STACK.md; не утверждён окончательно |
| Решение о стеке | Предложение D001 в DECISIONS.md |
| Слои архитектуры и контракты | Требования раздела 04 записаны в ARCHITECTURE.md и D002; реализация отсутствует |
| Графический редактор и UX | Требования раздела 05 записаны в EDITOR_UX.md и D003; реализация отсутствует |
| Save System & Checkpoint Editor | Обязательные требования раздела 06 записаны в SAVE_SYSTEM.md и D004; реализация и тесты отсутствуют |
| Игровые системы и расширения | Требования раздела 07 записаны в GAME_SYSTEMS.md и D005; реализация отсутствует, требуется проверенная основа |
| Шаблоны игр T01–T10 | Требования раздела 08 записаны в GAME_TEMPLATES.md и D006; шаблоны, конструктор и smoke-тесты отсутствуют |
| Codex / AI Integration и безопасность | Требования раздела 09 записаны в AI_INTEGRATION.md и D007; API, адаптер, sandbox и AI Game Tester не реализованы |
| Прототип 00.3 | Windows x64/MSVC/D3D11 spike собран; CTest 4/4; результат ограничен этой средой |
| Дорожная карта | Раздел 10 записан в ROADMAP.md и D008; этапы 00–10 запланированы |
| Структура репозитория | Добавлены engine/core, engine/scene, editor, tests/unit и tests/smoke; прочие подсистемы добавляются по мере реализации |
| Лицензия проекта и third_party | Лицензия не выбрана; политика зависимостей не согласована |
| Подэтап 00.1 | Минимальное приложение, CMake/CTest; чистая baseline сборка и 3 CTest проверены в рамках 00.3 |
| Подэтап 00.2 | Локальные проверки/PowerShell-тесты и отчёт реализованы; полная C++/format приёмка того подэтапа не подтверждена, clang-tidy пропущен |
| Подэтап 01.1 | EngineContext, Result/Error, Logger, ConfigurationService реализованы; unit-тесты добавлены |
| Подэтап 01.2 | IEngineModule и ModuleRegistry: зависимости, порядок lifecycle, rollback init и обратный shutdown; unit-тесты добавлены |
| Подэтап 01.3 | EventBus, TimeService/fixed timestep и ModuleRegistry::tick реализованы; unit-тесты добавлены |
| Подэтап 01.4 | Typed CommandRegistry, версия/type checks, validation, execute/result и audit реализованы; unit-тесты добавлены |
| Подэтап 01.5 | In-memory transactions, transactional commands, Undo/Redo, rollback/barrier semantics реализованы; unit-тесты добавлены |
| Исходники и API | Core API 01.1–01.5; in-memory Scene Model, Hierarchy/Scene View и Inspector Name/ID schema; entity.set_property v1 транзакционная; файловый формат Astro Scene v1. Components, Transform и миграции сериализации не реализованы |
| Сборка и тесты | Windows x64/MSVC Debug сборка успешна; CTest 9/9, включая scene.file.integration и editor.smoke с Undo/Redo, сохранением/открытием и ошибочным файлом; результаты в build/02.4-build.log и build/02.4-ctest.log |
| Политика проверок и документации | Требования записаны в TESTING.md и D009; это не результаты выполненных тестов |
| Недостающие документы целевого комплекта | docs/FILE_FORMATS.md, docs/TEMPLATE_GUIDE.md |
| Совместимость и лицензии зависимостей | Не проверены |

Сборка проверена из Visual Studio 2022 Developer Command Prompt x64; инструменты доступны через среду VS, хотя в обычном PowerShell не все присутствуют в PATH.

00.1/00.2, 00.3, 01.1–01.5 и 02.1–02.4: см. отчёты этапов. Следующий рекомендуемый подэтап — 03.1.

## Подэтап 02.1
Добавлен optional astro_editor: окно, docking, меню и project.astro. Создание проекта отказывает для существующего каталога до каких-либо изменений; файл записывается во временный файл и публикуется без замены. Некорректное открытие не меняет активный проект. На завершении 02.1 CTest 6/6, включая smoke этих сценариев. См. [отчёт 02.1](reports/02.1.md).

## Подэтап 02.2
Реализованы in-memory Scene/Entity hierarchy, защита от циклов, создание root/child entity и удаление поддерева; редактор показывает Hierarchy и выбираемый structural Scene View. CTest 7/7. Scene persistence, component system и пространственный Transform не входят в подэтап; см. [отчёт](reports/02.2.md).

## Подэтап 02.3
Добавлены schema properties Name и read-only ID, Inspector и команда `entity.set_property` v1. Изменения Inspector проходят через CommandRegistry; интеграционные проверки покрывают validation и выполнение. CTest 8/8. Undo/Redo, компоненты и сериализация ещё не подключены; см. [отчёт](reports/02.3.md).

## Подэтап 02.4
Astro Scene v1 сохраняет ID, имя сцены, иерархию и имена сущностей. Редактор предоставляет Ctrl+S/O/Z/Y (также Ctrl+Shift+Z), диагностику и блокирует смену проекта/сцены при несохранённых изменениях. Undo/Redo покрывает транзакционное изменение Name; создание/удаление сущностей не включено в историю. CTest 9/9; подробности и ограничения — [отчёт 02.4](reports/02.4.md).

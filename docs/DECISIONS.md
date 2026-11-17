# Журнал решений

## D001 — Предварительный технологический стек

- Дата: 2026-11-12.
- Статус: предложение; окончательное принятие отложено до прототипа 00.3.
- Основание: раздел 03 требований пользователя.
- Предложение и критерии проверки: [TECH_STACK.md](TECH_STACK.md).
- Ограничения: не фиксировать неподтверждённые версии; кандидаты не становятся обязательными зависимостями автоматически.
- Требование к архитектуре: адаптеры рендеринга, физики, звука и импорта должны позволять заменять библиотеки без переписывания ядра.
- Проверка: совместимость, лицензии и фактическая сборка ещё не подтверждены. Установки и загрузки зависимостей не выполнялись.

## D002 — Слои архитектуры и контракты

- Дата: 2026-11-12.
- Статус: архитектурные требования пользователя; реализация отсутствует.
- Основание: раздел 04 и принципы P01–P14.
- Решение: единый Typed Command API перед Document/Scene Model и Engine Core; ядро без окна, необязательные runtime-модули, отдельные Editor и Export.
- Контракты Core, команд, сцен, ресурсов, префабов, сериализации и runtime описаны в [ARCHITECTURE.md](ARCHITECTURE.md).
- Ограничения: AI не обходит команды; необратимые runtime-действия документируются; исходники ресурсов сохраняются; Play Mode изолирован от редактируемой сцены.
- Следствия: конкретные API и схемы требуют дальнейшего проектирования и проверки; перечисление компонента или команды не означает их готовность. Предварительный стек D001 не становится окончательным.

## D003 — Графический редактор и UX

- Дата: 2026-11-12.
- Статус: требования пользователя; реализация отсутствует.
- Основание: раздел 05, P01, P04, P05 и P10.
- Требования к областям редактора, рабочим пространствам, операциям, материалам, камерам и UI записаны в [EDITOR_UX.md](EDITOR_UX.md).
- Решение: редактирование свойства через GUI эквивалентно Command API; необходимы Undo/Redo, история и обработка конфликтов и несохранённых правок.
- Ограничения: узловой редактор шейдеров и кат-сцены предусмотрены позже; макеты и очередность реализации не утверждены.

## D004 — Выборочное сохранение и Checkpoint Editor

- Дата: 2026-11-12.
- Статус: обязательные требования пользователя; реализация отсутствует.
- Основание: раздел 06.
- Решение: разработчик явно выбирает сохраняемые объекты, компоненты и свойства через профили/правила. Автоматическое сохранение всего мира без этого выбора запрещено.
- Save System & Checkpoint Editor обязателен как модуль продукта. Правила включения в экспортируемую игру требуют дальнейшего определения с учётом P02.
- Режимы «только точка возрождения» и «запись полного выбранного состояния» разделены; respawn настраивается отдельно. Временные эффекты по умолчанию не сохраняются.
- Настройки хранятся в проекте, прогресс игрока — отдельно в пользовательских каталогах платформы. Требуются атомарная запись, резервная копия, версии, миграции и диагностика повреждений; защита от читов не обещается.
- Полный перечень требований и будущих проверок: [SAVE_SYSTEM.md](SAVE_SYSTEM.md).

## D005 — Игровые системы после проверенной основы

- Дата: 2026-11-12.
- Статус: требования пользователя; реализация отсутствует.
- Основание: раздел 07.
- Решение: игровые системы и расширения из [GAME_SYSTEMS.md](GAME_SYSTEMS.md) добавляются только после проверенной основы и по явно назначенным подэтапам.
- Ограничения: отдельный core для машин не создаётся; сохранение состояния NPC выполняется по выбору; возможности воды зависят от бюджета платформы.
- Следствия: критерии готовности основы, порядок реализации и численные бюджеты ещё предстоит определить. Новые зависимости этим решением не утверждены.

## D006 — Работающие модульные шаблоны игр

- Дата: 2026-11-12.
- Статус: требования пользователя; реализация отсутствует.
- Основание: раздел 08.
- Решение: каждый шаблон T01–T10 создаёт минимальную работающую игру, изменяемую через редактор и Codex, с инструкцией и автоматическими smoke-тестами.
- Каталог и условия приёмки: [GAME_TEMPLATES.md](GAME_TEMPLATES.md).
- Ограничения: шаблоны не создаются до реализации необходимых подсистем; оружие можно отключить с сохранением FPS-контроллера; камеры и управление переиспользуются между жанрами.
- Следствия: порядок реализации, структура пакетов и конкретные smoke-сценарии определяются в будущих назначенных подэтапах.

## D007 — Необязательная AI-интеграция с ограниченными полномочиями

- Дата: 2026-11-12.
- Статус: требования пользователя; реализация отсутствует.
- Основание: раздел 09.
- Решение: локальный AI Editor API через типизированные команды и отдельный адаптер Codex; игра и редактор запускаются без интеграции.
- Пайплайн: запрос → план → проверка схемы/разрешений → dry-run/diff → подтверждение рискованных операций → транзакция → тесты → отчёт.
- Ограничения: разрешённые директории, sandbox сборок/тестов, ограниченный доступ к секретам и сети, аудит, контроль затрат/времени и остановка долгих операций.
- AI Game Tester использует детерминированные сценарии и собирает свидетельства регрессий; отсутствие ошибок не гарантируется. Оптимизации проверяются на поведение и визуальные отличия.
- Полные требования: [AI_INTEGRATION.md](AI_INTEGRATION.md). Механизмы безопасности и конкретные технологии пока не выбраны и не проверены.

## D008 — Дорожная карта и критерии приёмки

- Дата: 2026-11-12.
- Статус: план пользователя; подэтапы реализации не начаты.
- Основание: раздел 10 из приложенного текста.
- Решение: этапы 00–10, подэтапы и критерии приёмки сохранены в [ROADMAP.md](ROADMAP.md). Каждый подэтап запускается по явному указанию и завершается отчётом и остановкой.
- Уточнение D005/D006: порядок работ теперь задан дорожной картой; конкретные тестовые сценарии, численные бюджеты и детали реализации уточняются в соответствующих подэтапах.
- Ограничения: стек не утверждается до 00.3; портирование подтверждается реальными целевыми toolchains, Windows сохраняет работоспособность; iOS-сборка на Windows без совместимого Mac не обещается.
- Наличие документации не подтверждает приёмку 00.1: необходимы минимальный app и тест, чистая конфигурация CMake, сборка, CTest и инструкции Windows.

## D009 — Проверки каждого этапа и готовность функции

- Дата: 2026-11-12.
- Статус: требования пользователя; исполняемая тестовая инфраструктура отсутствует.
- Решение: применять unit/integration/smoke/regression-проверки и специальные проверки графики, производительности, сохранений и безопасности согласно [TESTING.md](TESTING.md).
- Каждая готовая функция имеет документацию пользователя, пример, тест и известные ограничения. Целевой комплект документов перечислен в TESTING.md; отсутствие документов и тестов отмечается явно.
- Ограничения: бюджеты производительности утверждаются отдельно; визуальные допуски пока не определены. Невыполнимые проверки требуют объяснения и не считаются пройденными.

## D010 — Предварительная структура репозитория

- Дата: 2026-11-12.
- Статус: предложение пользователя; структура реализации ещё не создана.
- Основание: раздел 12.
- Предложение сохранено в [REPOSITORY_STRUCTURE.md](REPOSITORY_STRUCTURE.md); изменения допускаются с обоснованием.
- Согласование требований: docs/TEMPLATE_GUIDE.md остаётся обязательным согласно D009, несмотря на отсутствие в списке раздела 12. Существующие документы и .gitignore сохраняются.
- Ограничения: LICENSE только после выбора лицензии; third_party только по согласованной политике зависимостей. Ни лицензия проекта, ни политика пока не выбраны.
- Следующий подэтап реализации структуры — 00.1, по явному указанию пользователя.

## D011 — Минимальная основа сборки 00.1

- Дата: 2026-11-12. Основание: явное назначение 00.1 пользователем.
- Созданы отдельная цель astro_headless в engine/runtime, корневой CMake и CTest smoke-тест headless.startup без внешних библиотек. C++20 обязателен, расширения языка отключены.
- Выбран минимальный CMake 3.20 и presets schema 2; это требование конфигурации, а не подтверждение проверенной версии toolchain. Windows preset использует Ninja и уже настроенное окружение компилятора.
- Создаются только каталоги с содержательными файлами; остальные из предложения D010 добавляются по мере реализации. Пустые модули и фиктивный core не создаются.
- Используется smoke-тест процесса: ядра для unit-тестов ещё нет. Дальнейшие группы тестов остаются обязательными по D009.
- Существующее ТЗ сохранено. Приёмка сборки отложена: CMake/CTest и компилятор не обнаружены; установки не выполнялись. 00.2 не начат.

## D012 — Локальные проверки и диагностика 00.2

- Дата: 2026-11-12. Основание: явное назначение 00.2 пользователем при незавершённой инструментальной приёмке 00.1.
- Выбран локальный PowerShell 7+ pipeline: новый каталог сборки каждого запуска, JSON-отчёт, версии инструментов, SHA-256 входных файлов, отдельные stdout/stderr, тайм-ауты и ненулевые коды при ошибках/неполной проверке. CI-сервис не настраивался.
- clang-format проверяет исходники без перезаписи; clang-tidy включается при наличии через CMake option. Инструменты не устанавливаются; неподтверждённые версии не фиксируются.
- Логирование ограничено уровнями сообщений минимального CLI. stdout получил префикс [info]; для потребителей точного текста нужна адаптация. Командный API движка отсутствует и не изменён. --help возвращает 0, неверные аргументы — 2, сбой вывода/исключение — 1. Logger/Result этапа 01 не реализуются.
- 5 проверок запуска процессов и 9 проверок реального отчёта прошли. Полная приёмка C++ и форматирования остаётся заблокированной отсутствием инструментов; 00.3 не начат.

## D013 — Результат технического spike SDL3/bgfx/ImGui

- Дата: 2026-11-13. Основание: явное назначение 00.3 и отдельное разрешение пользователя на установку инструментов/загрузку библиотек.
- Windows x64/MSVC/SDK сборка работает с SDL 3.5.0 исходников, bgfx, bx, bimg и Dear ImGui docking `1.93.1 WIP`; ревизии приведены в [reports/00.3.md](reports/00.3.md).
- SDL3 создаёт окно и передаёт ввод; bgfx D3D11 обслуживает swap chain и рисует ImGui draw lists; ImGui docking строит панели. Отдельный renderer backend bgfx необходим, поскольку upstream SDL3 backend ImGui сам по себе не реализует bgfx renderer.
- Сборка и CTest spike прошли (4/4); автоматический сценарий подтвердил кадры, docking, клавиатуру, resize, draw calls. Решение подтверждено только для этого набора ревизий и окружения.
- Статус предварительного стека остаётся предварительным. Не подтверждены другие ОС/бэкенды, release package, размер release, полный транзитивный аудит лицензий и ручное UX-тестирование drag-and-drop. ImGui выбранной ревизии помечен WIP.
- Локальные зависимости остаются в игнорируемом `build/spike-deps`; `ASTRO_BUILD_STACK_SPIKE` выключен по умолчанию. Следующий этап 01 не начат.

## D014 — Базовые сервисы ядра 01.1

- Дата: 2026-11-13. Основание: пользователь начал этап 01; выполнен только первый подэтап 01.1.
- Core выделен в отдельную библиотеку `astro_core`, независимую от SDL, bgfx и GUI. `EngineContext` явно владеет Logger и ConfigurationService.
- `Result<T>` и `Result<void>` используют ErrorCode + сообщение; ошибки возвращаются как значения.
- Logger использует sink injection и порог severity, с простым stderr/clog sink по умолчанию.
- Для конфигурации выбран минимальный UTF-8 key=value формат без внешней зависимости. При загрузке состояние заменяется только после полного успешного разбора; это не утверждение окончательного формата сериализации.
- На момент завершения 01.1 объём не включал ModuleRegistry, lifecycle, EventBus, TimeService, команды, JSON, редактор или игровые системы; ModuleRegistry принят отдельно решением D015.

## D015 — Контракт ModuleRegistry 01.2

- Дата: 2026-11-13. Основание: пользователь продолжил следующим подэтапом после 01.1; выполнен только 01.2.
- `IEngineModule` предоставляет имя, статический список имён зависимостей и `initialize`/`shutdown` через общий `EngineContext`; `ModuleRegistry` владеет модулями через `unique_ptr`.
- Зависимости разрешаются детерминированным DFS/topological traversal в порядке регистрации и указанном порядке зависимостей. Неизвестные, пустые/дублирующиеся, самозависимые зависимости и циклы отклоняются до начала инициализации.
- Ошибка/исключение initialize возвращается через `Result`; уже успешно запущенные модули получают shutdown в обратном порядке. Модуль с неуспешным initialize обязан освободить собственные частичные ресурсы самостоятельно.
- Успешный lifecycle одноразовый: после него реестр нельзя менять или запускать повторно. Ошибка shutdown агрегируется, но не прерывает завершение остальных модулей.
- `tick` и его политика расписания не входят в 01.2; они остаются для 01.3. Тесты покрывают основные ошибки, порядок и rollback. Потокобезопасность и динамическая регистрация во время исполнения не предоставляются.

## D016 — EventBus и fixed timestep 01.3

- Дата: 2026-11-13. Основание: пользователь назначил следующий подэтап после 01.2; выполнен только 01.3.
- EventBus — header-only typed API на C++ type identity с монотонными subscription IDs. Dispatch синхронный и не потокобезопасный. Snapshot даёт определённое поведение при subscribe/unsubscribe внутри callback; исключения callback агрегируются как `InternalError` после продолжения рассылки.
- TimeService получает реальный frame delta от вызывающего кода, принимает только конечные неотрицательные значения, ограничивает frame delta, выдаёт число fixed steps до настраиваемого catch-up cap, остаток для интерполяции и величину отброшенного времени. Настройки по умолчанию: 1/60 s, clamp 0.25 s, максимум 8 шагов на advance.
- Pause не продвигает simulation time; при переходе в паузу дробный остаток accumulator очищается. Интерполяция/рендеринг не реализованы.
- `ModuleRegistry::tick(fixedDelta)` последовательно обновляет запущенные модули; вызывающая сторона использует `FrameTiming.fixedStepCount` для количества вызовов. Ошибка tick возвращается вызывающей стороне и не запускает автоматический shutdown.
- Тесты покрывают публикацию/ошибки callbacks, мутацию подписок во время dispatch, clamping, catch-up, dropped time, паузу, валидацию и порядок module tick. Потокобезопасность EventBus и измерение wall clock в ядро не входят.

## D017 — Ранний typed CommandRegistry 01.4

- Дата: 2026-11-13. Основание: пользователь назначил следующий подэтап после 01.3; выполнен только 01.4.
- Схема команды содержит dotted ID и положительную версию; при регистрации фиксируются точные C++ типы payload/output, обязательные validator и executor. ID уникален в пределах registry; одновременно хранится одна версия команды.
- `validate` является validation-only/dry-run и не вызывает executor; `execute` повторно валидирует payload и возвращает `Result<Output>`. Ошибки неизвестного ID, версии и типов диагностируются отдельными ErrorCode. Исключения callbacks преобразуются в InternalError.
- Локальный audit сохраняет последовательность операций validate/execute и ошибок без payload. Он не является persistence или защищённым журналом.
- Межпроцессные/JSON schema, dry-run diff, AI permissions, операции над сценами, rollback/транзакции и Undo/Redo не входят в 01.4. Executor, который меняет состояние, а затем падает, автоматически не откатывается; до 01.5 такие команды ограничены.
- Registry не потокобезопасен; output должен быть копируемым или `void`; миграции версий и совместная регистрация версий пока не поддерживаются.

## D018 — Транзакции и история команд 01.5

- Дата: 2026-11-13. Основание: пользователь продолжил со следующего подэтапа после 01.4; выполнен только 01.5.
- `CommandTransaction` содержит последовательность именованных apply/undo callbacks. `TransactionHistory::commit` применяет шаги по порядку; при ошибке компенсирует успешный префикс в обратном порядке. Пустая транзакция — допустимый no-op без записи в историю.
- Undo идёт в обратном порядке, Redo — в прямом. Новая успешная транзакция после Undo удаляет redo-ветку.
- Ошибка одного undo/redo вызывает компенсацию уже выполненного подмножества; если компенсация не удалась, история очищается. Ошибка rollback при commit также очищает историю. Все ошибки восстановления возвращаются с диагностикой.
- Каждый отдельный callback обязан быть атомарным или самокомпенсирующимся: менеджер может откатить только шаги, завершившиеся успехом.
- `CommandRegistry::registerTransactionalCommand` строит шаги, которые registry применяет и сохраняет для Undo/Redo. Существующие `registerCommand` остаются совместимы, но после успешной валидации очищают историю до запуска executor и выступают undo-barrier, включая случай ошибки executor.
- Audit включает Undo/Redo и связывает их с ID/version команды, созданной транзакцией. История и audit остаются только в памяти; Scene Model, persistence, лимит истории, multi-user conflicts и интеграционные команды появятся позднее.

## D019 — Редактор 02.1
Optional ASTRO_BUILD_EDITOR использует локальные зависимости 00.3. project.astro хранит версию и UTF-8 имя. Создание в существующем каталоге запрещено. Renderer пока дублирует прототип; Scene Model не реализован. См. reports/02.1.md.

## D020 — Безопасное создание проекта

- Дата: 2026-11-13. Основание: исправление риска повреждения существующих файлов в узком исправлении 02.1.
- Создание допускается только в новом каталоге. `project.astro` полностью записывается во временный файл в этом каталоге и публикуется через Windows `MoveFileExW` с `MOVEFILE_WRITE_THROUGH`, без разрешения замены. При ошибке удаляется только временный файл; каталог удаляется только если он остался пустым.
- Чтение проверяет формат до изменения активного проекта. Неверная версия, синтаксис или ошибка доступа сохраняют прежнее состояние UI.
- Это ранний Windows-прототип, а не финальный межплатформенный файловый контракт. Поведение файловой системы вне проверенной Windows-среды отдельно не сертифицировано.

## D021 — Минимальная модель сцены 02.2

- Дата: 2026-11-13. В рамках 02.2 Scene содержит монотонный scene-local Entity ID, UTF-8 имя и необязательную ссылку на родителя; операции проверяют валидность ссылки и запрещают циклы.
- Удаление сущности с детьми по умолчанию отклоняется; рекурсивное удаление требует явного флага. Это предотвращает случайное удаление поддерева через обычный delete.
- Scene View сейчас является графическим structural preview на основе отношений parent/child. Она не задаёт координаты мира и не является runtime renderer; Transform/компоненты и отображение игровых сущностей остаются будущей работой.
- ID действуют только в памяти Scene и не переживают её пересоздание/перезапуск. Persistence, миграции, стабильные сохранённые ID, Command API и Undo/Redo остаются вне 02.2.

## D022 — Schema properties и Scene command 02.3

- Дата: 2026-11-13. Inspector не меняет Scene напрямую: он выполняет `entity.set_property` v1 через `CommandRegistry`, тот же адаптер доступен тестам и другим клиентам.
- В текущей минимальной модели схема содержит `name` (обязательный редактируемый UTF-8 string, default `Entity`, максимум 240 байт) и `id` (read-only unsigned integer в диапазоне ненулевых EntityId). Новые типы полей добавлять вместе с моделью, валидаторами, UI editor и тестами; variant сейчас не означает, что произвольные типы уже поддерживаются.
- Команда регистрируется как non-transactional и создаёт Undo barrier; отмена/повтор интегрируются отдельно в 02.4.
- Для согласования с предсобранными статическими SDL3/bgfx библиотеками MSVC runtime задаётся единообразно на верхнем уровне CMake (`/MT` или `/MTd` по конфигурации), включая engine и тесты. Новых внешних зависимостей нет.

## D010 — Минимальный формат сцен Astro Scene v1 (02.4)

Сцены редактора хранятся в UTF-8 текстовом формате `ASTRO_SCENE 1` с явно записанными Scene ID, Entity ID, parent ID и именами. Формат покрывает только существующую Scene/Entity модель и не является полной сериализацией компонентов или Save System. Запись выполняется через временный файл в том же каталоге и атомарную замену (Windows: MoveFileExW). Открытие сначала полностью разбирает и проверяет временную модель, затем заменяет активную сцену; ошибки диагностируются без подмены активного состояния. Миграций, резервных копий и crash-recovery временного файла пока нет.
## D011 — Transform как обязательный базовый компонент (03.1)

Каждая сущность получает ровно один типизированный `astro.Transform` версии 1 при создании. Transform хранит локальные position, Euler rotation (градусы) и scale в double-векторах; значения должны быть конечными. `ComponentId` уникален в пределах сцены, сохраняется на диске и не переиспользуется при удалении в текущем экземпляре сцены. Не добавлять фиктивный универсальный контейнер произвольных компонентов: будущие типы добавлять с отдельной схемой и явной диагностикой неизвестных данных. Формат файла поднят до v2, v1 мигрирует к default Transform.

## D012 — Локальный Asset Registry без image dependency (03.2)

AssetId назначается монотонно и сохраняется в проектном `.astro/assets.index`; источник идентифицируется относительным UTF-8 путём под `Assets/`, кэш производных данных отделён в `Library/Assets`. PNG/JPEG получают ограниченную проверку структуры/размеров и SHA-256 без добавления внешней библиотеки или платформенного codec API. Первая версия сохраняет byte-identical encoded copy; перекодирование/оптимизацию и декодирование пикселей не объявлять готовыми функциями. Изменение source требует явной команды reimport, сохраняющей ID. Пути и symlink должны разрешаться внутри проекта; malformed index не перезаписывать молча.

## D023 — SpriteRenderer и ранний Scene View preview (03.3)

- SpriteRenderer остаётся отдельным типизированным компонентом со стабильным `ComponentId`, `AssetId` и флагом видимости; AssetRegistry не зависит от Scene, а Scene использует public `AssetId` из `astro_assets`.
- `entity.set_sprite_renderer` v1 является транзакционной командой, поддерживает Undo/Redo. Astro Scene v3 записывает компонент; загрузчик сохраняет чтение v1/v2, а сохранение мигрирует при явной записи.
- Windows Editor использует системный WIC как локальный декодирующий adapter и BGFX для текстурного preview, без добавления сторонней библиотеки. Лимит preview — 16384 на сторону и 64 млн пикселей.
- Изображение показано как preview выбранного компонента в Inspector; это не пространственная сцена, runtime renderer, кроссплатформенный decoder или гарантированный golden-image результат.

## D024 — Prefab assets, instances and updates (03.4)

- Prefab assets use a project-local numeric ID and a versioned UTF-8 `.astro-prefab` file; Transform/SpriteRenderer data is copied into the asset, nested links remain PrefabId references.
- Nested prefab links expand beneath their owning node. Repeated PrefabId in the active expansion stack is a cycle; depth is limited to 64 and expansion to 100000 Scene entities. Local hierarchy depth is limited to 256.
- Scene v4 stores each instance root, flattened source key → EntityId mapping and explicit typed Name/Transform/SpriteRenderer overrides. v1/v2/v3 scenes still load and upgrade only on explicit save.
- Update computes/prevalidates the new graph on a Scene copy, keeps IDs for source keys that still exist and applies base values before overrides. Overrides whose source key disappears reject the update; the live Scene stays unchanged. Removed prefab nodes with non-prefab children also reject rather than delete those children.
- `prefab.instantiate`, `prefab.set_override`, and `prefab.update_instance` are transactional. Undo/Redo keeps full Scene snapshots for this early implementation; memory use scales with scene size. Editor authoring UI and merge of simultaneous source/instance edits remain future work.

## D025 — Asset dependency graph and derived package prototype (03.5)

- Dependency edges are explicit AssetId links; scene/prefab dependency extraction is not automatic yet. Cycles and invalid IDs are rejected; the sorted transitive closure is deterministic.
- The initial package preserves validated encoded PNG/JPEG bytes and metadata, tags a target profile in the content hash, and writes beneath ignored `Library/Derived`. Cache reuse requires exact byte equality; damaged output is atomically replaced.
- Target tags do not claim platform codec compatibility. Texture transcoding, platform toolchain builds, runtime package loading, and full game export remain separate work. No external dependency was added.

## D026 — Runtime Session and isolated Play Mode (04.1)

- Play owns a deep value-copy of the authoring Scene. Runtime callbacks cannot mutate editor state unless external state is captured by the callback; that remains the caller's responsibility.
- Runtime Session controls fixed-tick dispatch and explicit Stopped/Playing/Paused/Faulted states. Manual single-step is legal only while paused and advances the simulation clock once.
- A callback error faults the session and retains the runtime Scene for inspection; Stop clears it. Rendering, input, editor controls, and actual game systems are not implied by this controller.

## D027 — Input Mapping and SDL3 adapter (04.2)

- Action bindings and state aggregation stay independent of SDL/window APIs; input adapters submit physical state to `InputMapping`.
- The initial SDL3 adapter translates event-queue key scancodes, mouse buttons/motion/wheel, and gamepad buttons/axes/removal. It remains optional with the editor target and is not linked into headless core.
- No persistent user profile, remapping UI, device enumeration/ownership, vibration, or non-Windows adapter is claimed. Device behavior was validated with synthetic SDL events only.

## D028 — Runtime Camera Rig pose and follow contract (04.3)

- Camera mode presets produce a tested world-space pose convention; target follow uses the target entity's stored Transform position plus world-space offset.
- Smoothing is exponential and frame-delta aware; optional bounds clamp both desired and resulting camera position. Zoom is a scalar range, not a projection/FOV contract.
- No look-at except FirstPerson's rotation inheritance, world-transform hierarchy composition, projection matrices, Scene camera component, serialization, renderer adapter, or editor UI is claimed. The rig is useful/testable pose logic but not a rendered camera feature.

## D029 — Runtime interaction behaviors and typed events (04.4)

- `InteractionSystem` stores code-defined runtime behavior bindings keyed by EntityId; it reuses the typed core EventBus for sequenced Requested/Activated/Rejected events rather than adding a second event transport.
- Eligibility checks actor/target existence, enabled state, Transform and Euclidean range before invoking the behavior callback on the supplied runtime Scene.
- Bindings are not Scene-serialized and are not editor-authored; physics triggers, UI prompts, input routing and undo are not implied. Callbacks and post-activation observers can cause non-transactional side effects; the caller owns the runtime copy and recovery policy.

## D030 — Minimal dependency-free runtime 2D physics (05.1)

- `PhysicsWorld2D` is a separate optional C++20 target and introduces no third-party physics dependency. 2D body/collider data remains runtime-owned and binds to one SceneId; callers step it inside RuntimeSession using the play Scene copy.
- The first verified slice uses static/dynamic bodies, gravity, axis-aligned boxes in XY, discrete pairwise overlap checks, basic positional correction and EventBus Enter/Stay/Exit notifications. Triggers report overlaps without resolving them.
- This is deliberately not a full physics engine. Rotation, mass/material response, CCD, joints, persistence, editor authoring and rendering are deferred; high-speed tunneling and O(n²) scaling are known limitations.

## D031 — Backend-neutral Game UI with optional SDL3/ImGui adapter (05.2)

- `GameUiModel` is always available as a dependency-light runtime state/controller API. It coordinates menu/pause transitions with RuntimeSession, exposes HUD/settings data, and emits typed notifications.
- Visual presentation is an optional `ASTRO_BUILD_GAME_UI_DEMO` target using existing SDL3 and Dear ImGui cache. It is independent of Editor source/target and is not linked into the default headless runtime.
- HUD and settings remain in memory. Audio, persistence, localization, gamepad UI navigation and non-Windows adapters are not claimed. Observer failure after a transition does not roll back the model/session state.

## D032 — Minimal puzzle rules, scalar animation sampling, optional SDL audio cues (05.3)

- Puzzle rules and scalar animation sampling live in dependency-light `astro_runtime_puzzle`; state is currently runtime-only. Match-three uses orthogonal swaps, run clearing, gravity/refill, seeded randomness, cascade scoring and score thresholds. It is not yet connected to a visual board or project/scene serializer.
- The audio adapter reuses SDL3 already present in the optional demo profile. It queues short generated sine cues to the default device rather than introducing an audio library or claiming a general asset playback/mixing system. Failure to open audio is reported by the demo and does not block gameplay UI.
- SDL audio smoke uses the dummy driver; it validates queueing through SDL's output stream, not audible speaker output. No new external dependency or installation was needed.

## D033 — Explicit selective SaveProfile and bounded Save v1 slots (05.4)

- SaveProfile is opt-in and starts with no fields. In v1 the caller names Entity IDs and property IDs directly; no whole-scene default, tag/group inference, or implicit persistence is permitted. Changing the selected field set changes a profile fingerprint, and compatibility requires matching profile ID/version/fingerprint, game version and Scene ID.
- `SaveStore` receives an application-owned user-data root and generates numeric slot filenames. It uses a bounded UTF-8 text format, CRC32 for accidental corruption detection, unique temp files and atomic replacement. A prior parseable slot is atomically preserved as `.bak`; load falls back to it if primary data is malformed. CRC32 is not anti-cheat protection.
- Save v1 supports entity name, scalar Transform values and SpriteRenderer visibility only. Unsupported gameplay components and property migrations are rejected/deferred. Missing entities/components are reported and skipped; restore is applied to a Scene copy before publication.
- No JSON dependency, editor UI, automatic trigger, platform-specific root discovery, checkpoint, respawn, or migration framework was added. `ASTRO_BUILD_SAVE_RUNTIME` gates the library for non-test builds.

## D034 — Runtime-only checkpoint triggers over explicit save profiles (05.5)

- Checkpoint definitions bind to a Scene EntityId and XY AABB centered on that entity's stored Transform position. Detection is edge-triggered (outside → inside); hierarchy/world transforms, Z extent, physics query, and persistent component serialization are not part of v1.
- Runtime condition is a caller-supplied `Result<bool>` callback. A checkpoint selects either `RespawnOnly` or `SaveSelectedProfile`; the latter must name a registered profile and valid SaveStore slot and saves before activation becomes current. OneShot becomes consumed only after a successful activation; Repeatable can trigger again after leave/re-enter.
- Respawn is explicitly invoked by host gameplay. Default policy restores only the configured checkpoint/fallback position. SelectedProfile restoration is a separate opt-in policy and is allowed only for a SaveSelectedProfile checkpoint; it applies into a Scene copy before the checkpoint spawn position is assigned.
- Conditions and EventBus observers may have external side effects that are not rolled back. Respawned event observers run after the runtime Scene has been replaced. Definitions and active state are runtime-only and must be registered against the Play Scene. No editor authoring, death detector, visual/audio behavior, health/inventory rules, or no-code condition graph was added.
- `ASTRO_BUILD_CHECKPOINT_RUNTIME` gates the target for non-test builds and also enables its SaveStore dependency; no new third-party dependency was added.

## D035 — Windows package for the current Game UI prototype (05.6)

- `ASTRO_BUILD_WINDOWS_RUNTIME_PACKAGE` is opt-in and currently requires Windows x64/MSVC plus `ASTRO_BUILD_GAME_UI_DEMO=ON`. It uses CPack ZIP; default/headless builds remain unaffected.
- The archive contains the statically linked `astro_game_ui_demo.exe`, a runtime README, and the SDL3/Dear ImGui upstream license texts from the existing local dependency cache. Missing license texts fail configuration instead of silently distributing an incomplete package. No DLL is currently required in the Release archive; OS system libraries remain platform requirements.
- A CTest smoke builds the archive, extracts it into a fresh unique directory and starts the packaged executable with `--smoke` from that directory. It does not use a project/source-tree resource. The Release ZIP is a package of the current Game UI prototype only.
- AstroEngine's own license remains undecided. This package does not establish project licensing or satisfy third-party license review for all dependencies. The visible multi-level puzzle and connected SaveProfile/checkpoint progression remain absent, so Stage 05 acceptance is still open.

## D036 — Keep Visual Logic 06.1 as a pure typed dataflow prototype

- The first Visual Logic iteration is an optional C++20 graph/evaluator with typed Number/Boolean/String pins, deterministic evaluation, bounded node/text sizes, and a synchronous step debugger. It has no new third-party dependencies.
- The editor uses typed menus for connections to keep this substage small. Graph files, undo, execution-flow/branch nodes, runtime bindings, gameplay commands, Scene mutation, and drag-wire authoring are deferred; no UI action should imply these are available.
- Keep the evaluator independent of `CommandRegistry` and Scene until the graph model, persistence, and transactional semantics can be designed and tested together. This prototype does not meet Stage 06 acceptance by itself.

## D037 — Bound the first UI Editor to a previewable in-memory document (06.2)

- The UI document model supports stable local element IDs, Panel/Text/Button elements, Dark/Light theme tokens, responsive anchors and pixel offsets, and typed bindings from `Entity.Name` to Text or `SpriteRenderer.Visible` to Visible.
- Binding refresh reads Scene values into the UI document and never writes to Scene. It validates all bindings before applying refreshed values, so a stale source does not partially update the document.
- The first authoring panel is an in-memory editor prototype. Versioned UI-file persistence, gameplay/runtime rendering, more binding sources, navigation/input, localization, nested interactive layout editing, and custom theme authoring are deferred. This does not satisfy full Stage 06 acceptance.
- No additional third-party dependency is introduced; the Editor panel uses the existing ImGui integration.

## D038 — Separate animation sampling from puzzle runtime and expose an editor preview (06.3)

- `AnimationTrack` and `AnimationTimeline` live in the optional `astro_runtime_animation` target. `ASTRO_BUILD_PUZZLE_RUNTIME` retains a public dependency on that target for compatibility; `ASTRO_BUILD_ANIMATION_RUNTIME` enables animation without the Puzzle module.
- Timeline authoring is limited to one bounded scalar track with strict key ordering, linear interpolation, scrub and simple play/pause. This is not a general animation graph or Scene-property binding system.
- Camera Presets reuses `CameraRig` modes and validation; the panel previews computed pose values only. It does not create a camera component or render a view.
- Both panels use existing editor dependencies; no new external library is added. State remains in memory until a versioned animation document format is designed.

## D039 — Keep Save System authoring explicit and runtime-compatible (06.4)

- Editor groups are explicit entity-ID sets; profile group rules flatten to the existing `SaveProfile` entity/property pairs before runtime use. No scene-wide or tag-inferred saving is introduced.
- The panel offers only Save v1 properties that the runtime can actually store. It validates component availability before compilation and reports unsupported/missing bindings instead of silently omitting them.
- Checkpoint configuration is validated by `CheckpointSystem`; the XY view is a Transform-based authoring preview. Restore testing uses an isolated Scene copy and a unique OS temporary directory.
- Profile/group/checkpoint authoring remains in memory in 06.4. Persisting it requires a versioned project document format and migration policy, so it is not added implicitly to Astro Scene v4 or player save slots.
- No external dependency is added. This bounded editor work does not complete Save System product requirements such as tags, templates, health/inventory, conditions, or respawn policies.

## D040 — Add a bounded 2D canvas before authoring game templates

- `Canvas2D` stores backend-neutral filled-rectangle commands, explicit RGBA colors, and layer order; camera projection is independent of SDL. The optional SDL3 adapter owns no window/renderer resources.
- A standalone Play preview composes existing `InputMapping` and `RuntimeSession`, updates only a runtime Scene copy at the fixed timestep, and demonstrates SDL rendering/input without adding a heavyweight dependency.
- This is a geometry-only foundation and does not establish SDL3 as the final engine renderer. It does not render Scene/SpriteRenderer assets or provide editor Play UI, physics controller, or game templates. Do not mark 06.5 complete based on this experiment.

## D041 — Connect root Scene sprites through an asset-resolving render boundary

- `astro_scene_render2d` converts visible root `SpriteRenderer` components into backend-neutral Canvas2D sprite commands. It reads dimensions from `AssetRegistry`, applies XY position/scale and Z rotation, and rejects missing assets, invalid transforms and child sprites instead of silently misrendering unsupported hierarchy transforms.
- `SDLCanvas2DRenderer` accepts a caller-supplied AssetId-to-texture resolver; the renderer does not own decoded assets. The Windows sample uses a bounded WIC BGRA decoder and caches SDL textures for the renderer lifetime. Source images remain in Assets; decoding reads the validated Library cache.
- The WIC decoder is platform-specific and Windows-only. Future platform adapters must supply their own decoder. Parent transforms, camera components, editor preview integration, streaming, batching and template gameplay remain deferred; this follow-up does not complete stage 06.5.

## D042 — Compose 2D hierarchy as affine textured quads and preview existing projects read-only

- Each sprite's local rectangle is transformed through the root-to-entity XY chain with `T * Rz * S` composition. Four world-space corners are kept in the backend-neutral draw command and SDL3 renders them with textured geometry, preserving shear caused by nested rotations and non-uniform scales. X/Y Euler rotations and Z position are outside this 2D view.
- `astro_2d_playground --project <directory> --scene <relative-file>` loads Astro Scene v1–v4 and an existing AssetRegistry. The scene path must resolve inside the canonical project root. Preview requires existing non-symlink `Assets`, `Library/Assets`, `.astro`, and `.astro/assets.index`; this avoids `AssetRegistry::open` creating a missing project skeleton. Scene, source, registry and cache are read-only during preview; stale/missing cached images fail with diagnostics rather than being rebuilt.
- Project preview auto-fits visible sprite bounds. It is standalone and Windows-only while WIC is the only image decoder. It does not add Editor Play UI, project mutation, asset rebuild, camera components, scene hierarchy editing, or game templates.

## D043 — Initial Scene/Game View integration (superseded by D044)

- Keep the existing bgfx/ImGui editor backend. Render each view into a bounded SDL software surface and upload BGRA pixels as a bgfx texture for ImGui; this connects the established Canvas2D/SceneSprite/SDL path without replacing editor rendering.
- Scene View keeps its hierarchy map as a separate tab. Game View displays a disposable `RuntimeSession` Scene copy with Play/Pause/Resume/Step/Stop; the Scene remains unchanged by Play. Until gameplay modules are deliberately connected, the fixed-step callback is empty.
- This CPU copy is a bridge with measurable per-frame cost, not the final GPU renderer. The editor renders SpriteRenderer assets only and has no selection overlays or non-Windows WIC decoder.

## D044 — Render Scene/Game View directly into bgfx framebuffers

- Benchmarking at 960x540 with 1000 sprites measured 7.995 ms Scene View and 7.378 ms Game View CPU p50, before the bgfx frame completes. The current pair submits 4,147,200 BGRA bytes per frame (about 237.3 MiB/s at 60 FPS); `bgfx::copy/updateTexture2D` CPU submission alone was 0.69–0.73 ms per pane. This excludes actual GPU transfer completion and is not a GPU-time measurement.
- Implemented as two bgfx framebuffer views. The adapter converts the backend-neutral sprite corners to transient vertex/index buffers, samples textures already uploaded by Editor, batches consecutive sprites sharing a texture, and renders both views before ImGui. This removes SDL software rendering and full-frame CPU uploads. SDL remains the independent runtime adapter.
- On the recorded final run with the same synthetic scenes, 1000-sprite CPU p50 is 4.809 ms Scene / 4.782 ms Game, versus 7.995 / 7.378 ms on the CPU bridge. Paired p50 falls from 15.373 to 9.591 ms (37.6%); conservative paired p95 sum falls from 18.271 to 11.408 ms (37.6%). bgfx GPU frame timing reports 0.074 ms p50 / 0.135 ms p95 for both offscreen views. Full-frame host upload is zero; 1000 sprites submit 184,000 bytes of transient vertex/index data for both views.
- The 1000-sprite CPU prep/vertex build is now the main measured cost (~5.3–5.6 ms per view). Benchmarks are synthetic and local to D3D11/MSVC Debug, and do not include the full ImGui composition in the CPU totals. Visual testing on another GPU/backend and broader alpha/resize golden coverage remain useful follow-ups.

## D045 — Validate Scene/Game framebuffer output with pixel predicates (06.11)

- The Editor smoke captures a temporary bgfx backbuffer TGA after resizing the window and validates minimum pixel counts for opaque red, opaque blue and alpha-blended green. This covers texture switching, blend state, framebuffer recreation and presentation through ImGui without checking in a GPU-specific golden image.
- Color ranges are used instead of exact image hashes because GPU/backend/color-pipeline differences can change edge pixels. The screenshot and generated test project are removed after the assertion; command logs are retained.
- The synthetic 960x540 D3D11 Debug benchmark includes up to 10,000 sprites, within the current 16-bit transient index ceiling. Dense-scene CPU cost is a measured limitation, not an approved frame budget or a claim about Release performance.
- Release renderer libraries build, but full Editor Release linking requires `bgfxRelease.lib`; the current dependency cache contains only `bgfxDebug.lib`. No dependency build or download was started in this substep.

## D046 — Use indexed, cached world transforms for dense 2D scene composition (06.12)

- Profiling showed that `Scene::transform(EntityId)` calls the Scene's linear `find`, so composing each sprite independently made a flat N-sprite scene O(N²). Parent resolution also used that linear lookup.
- `appendSceneSprites` builds a per-call EntityId-to-vector-index map and a vector-backed world-matrix state/cache. Parent affine transforms are composed once and reused by descendants; cycle/missing-parent/missing-Transform and finite-value diagnostics remain. The cache is scoped to one composition call, so editor edits cannot leave stale transforms across frames.
- For exact zero Z-rotation, local matrices avoid sin/cos. Axis-aligned matrix columns use absolute values instead of `hypot` only when the other coefficient is exactly zero; the general affine/shear path retains `hypot` and `atan2`.
- No Scene public API or serialized format changes. Existing hierarchy integration and full optional CTest pass. D3D11 Debug 10k CPU p50 falls from 142.218/141.326 ms to 26.086/24.348 ms for Scene/Game; optimized Release measures 4.913/4.682 ms. Measurements exclude ImGui composition and vsync.

## D047 — Report the complete VSync-limited Editor frame separately from view CPU cost (06.13)

- `astro_editor --benchmark` measures representative temporary scenes through SDL event polling, editor UI composition, both framebuffer views and `bgfx::frame()`. It reports 24 warm-up + 48 sampled frames per case and separates pre-frame Editor CPU, Scene/Game view CPU, bgfx frame call and total host wall time.
- Keep VSync enabled to measure the real interactive Editor path. On this D3D11 Release run, p50 wall time is ~16.6 ms and p95 ~17.3–17.6 ms; the host frame-call wait is ~14.6–14.8 ms. These are observed paced frame times, not uncapped throughput or pure GPU execution time. Driver BGFX latency counters were unavailable, and the reported GPU timer interval is not used as a standalone GPU workload claim.
- Benchmark scenarios are deterministic, generated under the benchmark-owned temporary project, and do not read or modify user game assets. They approximate common 2D content patterns but are not representative of a real game project; hardware, window compositor, background load and driver affect the result.

## D048 — Build SDL 2D adapters when Editor tests consume the playground (06.14)

- The 2D playground is added when either its standalone option is enabled or Editor testing is enabled. In the latter configuration, its runtime SDL renderer and WIC image adapter are required link targets even when `ASTRO_BUILD_2D_PLAYGROUND=OFF`.
- Derive the internal adapter-build condition from `ASTRO_BUILD_2D_PLAYGROUND` or `(ASTRO_BUILD_EDITOR AND BUILD_TESTING)`. This keeps standalone module opt-in while ensuring the enabled Editor smoke/integration consumer gets real CMake targets rather than unresolved library-name strings.
- Reconfigure and build all targets, then run the full Debug CTest suite to verify the link graph. Recorded result: build passed and **37/37** tests passed. See [06.14 report](reports/06.14-debug-link-and-real-scene-profile.md).

## D049 — Snapshot Puzzle runtime state without coupling the rules module to storage (06.5)

- `PuzzleGame` exposes a validated value snapshot containing board/rules/progress and the serialized `std::mt19937` engine state. Restore validates bounds, tile palette values, and complete RNG parse before publishing a game value.
- Keep filesystem format, user-data location, checksums, slots, and explicit SaveProfile selection in SaveStore; the puzzle rules module does not write files or silently persist the whole board.
- This is a storage integration seam only. It does not claim project save/load, editor authoring, or a complete T01 template. Unit + full Debug CTest passed; see [06.5 snapshot report](reports/06.5-puzzle-snapshot.md).

## D050 — Add explicitly selected typed custom values to SaveStore v2 (06.6)

- Save profiles may declare ASCII custom keys and their string/double/bool type. A save call must provide exactly the selected keys with matching types; this keeps profile selection explicit and prevents silently persisting arbitrary runtime data.
- Preserve Scene-only calls and `ASTRO_SAVE 1` for profiles with no custom values. Custom data uses `ASTRO_SAVE 2`; the parser continues to read v1. The profile fingerprint includes custom key/type selections.
- Bound custom keys to 128 bytes and strings to 64 KiB, reject non-finite numbers, and validate all custom records before publishing either the loaded Scene or caller state.
- This is a generic value map seam; it does not serialize PuzzleSnapshot or arbitrary components automatically. Integration and compatibility checks: [06.6 report](reports/06.6-save-custom-fields.md).

## D051 — Persist Puzzle snapshots through a separate optional SaveStore adapter (06.7)

- Keep `astro_runtime_puzzle` independent of files and `astro_runtime_save`. Add `astro_runtime_puzzle_save` only when tests are enabled or both modules are explicitly requested.
- The adapter serializes a `PuzzleSnapshot` into one profile-selected string key (`puzzle.snapshot` by default); it does not select fields or save a whole Scene implicitly. Other selected custom values remain explicit inputs/outputs.
- Give the Puzzle payload its own version (`ASTRO_PUZZLE_SNAPSHOT 1`) inside the existing checksummed SaveStore v2 slot. Unknown versions and malformed payloads fail; no migration is claimed.
- Load into a Scene copy and publish it only after SaveStore validation, payload parsing and `PuzzleGame::restore` all succeed. See the integration checks in [06.7 report](reports/06.7-puzzle-save-adapter.md).

## D052 — Run Puzzle as an optional Game View sample mode (06.15)

- Keep Puzzle rules in `astro_runtime_puzzle`; link the Windows Editor to that target only when it exists. `Play Puzzle` is available in test builds or when `ASTRO_BUILD_PUZZLE_RUNTIME=ON`, and the runtime target remains optional.
- Render the board in Game View, queue adjacent-cell selections there, and execute swaps from the `RuntimeSession` fixed-step callback. Pause holds queued input and Step executes one queued move.
- Keep Puzzle preview state separate from the authored Scene and from generic Scene Play. This is not a serialized Scene component, project template, or automatic progress-save policy.
- `editor.smoke` covers board rendering, accepted moves, Pause/Step and authoring Scene isolation. See [06.15 report](reports/06.15-puzzle-game-view.md).

## D053 — Expose explicit Puzzle snapshot slots in Game View (06.16)

- Reuse `astro_runtime_puzzle_save`; do not implement another encoder or persist Puzzle state as Scene fields. The Editor profile selects only the string key `puzzle.snapshot`.
- Offer manual slots 0–9. No automatic writes. For the built-in Editor preview, obtain the user-writable root through `SDL_GetPrefPath` and append `Saves/PuzzlePreview`; show the resolved directory. Do not store player progress in the project checkout.
- Keep the adapter optional: Editor links it only when the target exists; test configuration builds it automatically, and Editor + Puzzle explicitly enables it for non-test builds.
- The graphical smoke substitutes a unique temporary test-owned directory and removes it. It saves a board, mutates it, loads the selected slot, verifies exact snapshot restore, then continues Pause/Step regression. See [06.16 report](reports/06.16-puzzle-save-game-view.md).

## D054 — Store Puzzle preview SaveProfile and user-data subdirectory per project (06.17)

- Keep `project.astro` v1 unchanged. Store Editor-only Puzzle settings in optional `.astro/puzzle-save.settings` v1 with atomic replacement and strict parsing.
- Store profile ID/schema and a relative UTF-8 user-data subdirectory. Reject absolute/rooted paths and parent traversal; keep progress slots outside the project checkout under `SDL_GetPrefPath`.
- Default to profile `puzzle-preview` v1 and a deterministic folder derived from normalized project root. This distinguishes projects without a project-format migration; settings are persisted on Apply or the first Save.
- Keep the SaveProfile field explicit and narrow: only string `puzzle.snapshot`. Profile/schema edits do not delete or migrate old slots. The legacy shared `Saves/PuzzlePreview` path can be selected manually; no automatic file move occurs.
- `editor.smoke` checks default path isolation, settings round-trip, rejection without overwriting valid settings, settings UI visibility and existing slot round-trip. See [06.17 report](reports/06.17-project-puzzle-save-settings.md).

## D055 — Keep Puzzle level authoring separate from player progress (06.18)

- Model authored Puzzle content as `PuzzleLevelDefinition`, not as Scene entities or SaveStore slots. Keep the existing seeded PuzzleGame creation API intact and add a validated overload for exact initial tiles.
- Store project content under `Puzzle/Levels/<safe-ID>.astro-puzzle-level` with an explicit v1 format, strict bounds/UTF-8/tile validation and atomic replacement. Unknown versions fail with a diagnostic; no migration is claimed.
- Do not allow the current preview SaveProfile to load/save while an authored level runs: the slot fingerprint identifies profile/schema, not the Puzzle level. Keep existing sample slots available in the built-in sample mode; do not move or delete any user data.
- Scope the first GUI to one editable draft and its cells/parameters. Level library/campaign, delete/duplicate workflow, Undo for draft edits and per-level progress are future work. See [06.18 report](reports/06.18-puzzle-level-authoring.md).

## D056 — Bind Puzzle progress slots to authored level IDs (06.19)

- Keep the SaveStore container and `PuzzleSnapshot` payload unchanged. For authored levels, explicitly select custom string key `puzzle.level.id` and validate the saved value against the active level ID before publishing loaded state.
- Partition Editor slots under `PuzzleLevels/<ID>` in the project's existing SDL user-data namespace. Preserve the built-in sample profile and directory and do not move/delete existing user data.
- Reject unsafe/malformed IDs; level names do not determine storage identity. This provides per-level slot isolation, not a campaign/progression graph, automatic saving, or schema migration. See [06.19 report](reports/06.19-level-scoped-progress.md).

## D057 — Add a safe project Puzzle level catalog (06.20)

- Keep `ASTRO_PUZZLE_LEVEL 1` and the stable-ID filename layout unchanged. Catalog enumeration is bounded, sorted by ID, restricted to safe regular files inside the project, and returns per-entry diagnostics so one malformed file does not hide readable levels.
- Add create-only publication for new/duplicate IDs. Preserve the existing explicit Save/update behavior for an already opened same-ID draft. Duplicate and rename reject occupied IDs rather than replacing files.
- Rename writes and validates the new definition, rechecks that the original has not changed, and removes the old path only after the new file is available; on failure it attempts rollback and returns an error. Editor asks for confirmation. The old progress-save namespace remains untouched and is not migrated.
- Opening another level or resetting a changed draft requires explicit discard confirmation. No bulk deletion, campaign progression, or undo journal is added. Limits and tests are in [06.20 report](reports/06.20-puzzle-level-catalog.md).

## D058 — Specify a versioned Puzzle campaign document before implementing progression (06.21)

- Use a separate UTF-8 text file `Puzzle/Campaigns/<campaign-id>.astro-puzzle-campaign` with an explicit ordered list of stable level IDs and a separate `START` reference. List order is for presentation; it does not imply transitions.
- Keep v1 rules deliberately small: ordered `SCORE_AT_LEAST` branches followed by exactly one `DEFAULT` to another member level or `END`. Evaluate using the completed level's final score when the host requests one transition. Unknown conditions fail validation; no scripting callbacks or external state are implied.
- Require all references to resolve within the campaign and all levels to be reachable from START. IDs remain stable; future rename must not leave dangling campaign references. No campaign parser, runtime behavior, or Editor UI is claimed in this design-only slice.
- The complete proposed grammar, example, bounds, and future acceptance tests live in [PUZZLE_CAMPAIGNS.md](PUZZLE_CAMPAIGNS.md). The proposal needs technical review during implementation; v1 has no migration yet.

## D059 — Parse and persist the v1 Puzzle campaign format without implying progression (06.22)

- Keep the D058 text grammar and its explicit order/start/score/default model. Implement strict UTF-8 parsing/serialization and structural validation in `astro_runtime_puzzle`; unknown versions, conditions, malformed records, unsafe IDs, bad references, unreachable levels, and out-of-bound documents fail with diagnostics.
- `PuzzleCampaignFile` owns only project content at `Puzzle/Campaigns`; it validates authored level references and path containment and publishes via sibling temporary + atomic replacement. Player progress remains in the existing save system.
- This completes the data format contract only. It does not resolve score transitions, add Editor campaign UI, migrate schemas, or make level rename reference-aware. Tests and explicit limits: [06.22 report](reports/06.22-puzzle-campaign-parser.md).

## D060 — Resolve exactly one Puzzle campaign edge per completion request (06.23)

- Add a pure resolver over a validated campaign, completed level ID, and final score. Evaluate that source's descending score rules in declared order and return the first match; otherwise return its DEFAULT target.
- Represent `DEFAULT END` as a successful empty optional. Reject invalid campaigns and completion IDs that are not campaign members.
- Do not load the returned level or recursively consume another transition. The host owns loading and requests resolution again only after a later completion. Editor controls and automatic progression orchestration are not part of this substage; see [06.23 report](reports/06.23-puzzle-campaign-resolver.md).

## D061 — Make Puzzle campaign END terminal and preserve the result board (06.24)

- Add `PuzzleCampaignSession` as the host for authored Puzzle level data and a playable `PuzzleGame`. A successful move that reaches the current authored `targetScore` marks the level ready for explicit completion; the session resolves the active score and stages the next level/game before publishing them.
- If next-level read or game initialization fails, preserve the current completed board and allow retry after the asset is repaired.
- `DEFAULT END` transitions the session to `Ended`, retaining the final level and score for result UI. The ended session accepts no more moves or completion requests. Restart is explicit by creating a new session, which begins at campaign START. Do not implicitly restart or save progress here.
- The session loads Puzzle level files only; it does not load engine Scenes or integrate with Editor UI. See [06.24 report](reports/06.24-puzzle-campaign-session.md).

## D062 — Host Puzzle campaign sessions in Game View (06.25)

- Game View starts a `PuzzleCampaignSession` from the active project and an explicit campaign ID. Tile input enters the session only through the existing fixed-step runtime callback.
- Reaching a level target does not advance automatically. The view exposes Continue so users can inspect the completed board/score before the authored transition is resolved.
- `END` retains the final board/result in Game View; restart is explicit and creates a fresh session from START. Stop clears the active session.
- Campaign progress is not written to the standalone Puzzle SaveStore slots; do not imply persistence until a campaign save identity/format is designed. Campaign authoring remains file-based in this substage. See [06.25 report](reports/06.25-puzzle-campaign-game-view.md).

## D063 — Keep the campaign browser read-only and report broken level links (06.26)

- Add a bounded `PuzzleCampaignFile::list` API that distinguishes malformed documents from structurally valid campaigns with unavailable level assets. Preserve the parsed campaign definition when only an asset reference is broken so the Editor can explain the issue.
- Show campaign IDs/names, START, ordered level IDs, transition rules, and per-level asset status in a read-only Editor browser. Selecting a structurally readable campaign fills the Game View launch ID; launching still performs the stricter full reference validation.
- Do not rewrite project files during enumeration or diagnostics. Keep malformed files visible and cap directory enumeration; do not recurse or follow campaign symlinks. See [06.26 report](reports/06.26-puzzle-campaign-browser.md).

## D064 — Author campaigns through validated drafts and guard level renames (06.27)

- Extend the campaign browser with one explicit draft editor for ID/name, ordered level references, START, score thresholds, and DEFAULT/END targets. Confirm writes and route them through `PuzzleCampaignFile::write`; changing an ID writes a separate campaign and never removes the old one.
- Preserve unsaved draft edits unless the user explicitly confirms discard. Level rename checks every campaign: refuse when the old ID is referenced, when a campaign is unreadable, or when enumeration fails. The user updates and saves campaign references before renaming, then adds the new ID afterward. Do not perform multi-file rewrites or progress-slot migration implicitly.
- Physical UI click automation and campaign progress persistence are outside this slice. See [06.27 report](reports/06.27-puzzle-campaign-authoring-rename.md).
# D065 — Перевод Windows Editor через внутренний каталог (06.28)

- Поддержать English и Russian для интерфейса Windows Editor. Держать словарь внутри `editor/Localization.*`, без сторонней зависимости.
- Хранить выбор языка как версионированное пользовательское предпочтение SDL, отдельно от проекта и игрового содержимого; по умолчанию использовать English. Применять настройку сразу после Apply.
- Сохранять стабильные скрытые ImGui ID при переводе названий окон, чтобы переключение языка не ломало докинг.
- Не считать runtime/demo, API-диагностики, драйверные ошибки или пользовательский текст автоматически локализованными; эти границы явно документировать.
- Проверять прямые литералы UI-текста на наличие перевода, round-trip настроек и smoke отображения окна настроек на русском. См. [отчёт 06.28](reports/06.28-editor-russian-localization.md).

## D066 — Раздельные каталоги редактора и runtime UI (06.29)

- Сохранить локализацию Editor отдельной от игрового UI: проекты и игры не должны зависеть от Editor.
- В 06.29 `ui.language` использовал стабильные `en`/`ru` и действовал только в процессе demo. Это ограничение было снято в 06.30: язык хранится отдельно от проекта и игровых слотов согласно D068.
- Не переводить произвольный HUD/пользовательский текст. Известные ошибки GameUiModel и ImGui adapter переводятся точно; неизвестная runtime/Editor диагностика получает русскую метку с сохранением оригинальной технической детали.
- Не переписывать ошибки SDL/драйвера, пути или данные. Системный шрифт включать только при русской локали, чтобы сохранить существующий английский визуальный baseline. См. [отчёт 06.29](reports/06.29-runtime-and-diagnostics-localization.md).

## D067 — Универсальный AI-first профиль продукта

- Дата: 2026-11-14.
- Статус: принято как продуктовая цель; не является утверждением о текущем паритете функций с Unity, Godot, Unreal Engine или CryEngine.
- AstroEngine предназначен для модульной разработки 2D/3D-игр разных жанров с графическим редактором. AI-assisted authoring — ключевой пользовательский путь и отличие продукта, а не второстепенный чат-плагин.
- GUI, визуальная логика, AI и программные расширения используют согласованную модель и общий типизированный Command API. ИИ планирует, показывает diff, проверяет разрешения, применяет допустимые изменения и запускает тесты; рискованные действия требуют подтверждения и отката/транзакции согласно контракту.
- AI adapter/provider не обязателен для запуска Editor, Runtime и игры; не вводить привязку к одному провайдеру или постоянной сети. Безопасность, аудит, sandbox и ограничения доступа остаются обязательными требованиями AI-интеграции.
- Puzzle рассматривается как ранний vertical slice для проверки систем и сохранений, а не как конечный жанр движка. Подробное ТЗ обновлено в [PRODUCT_REQUIREMENTS.md](PRODUCT_REQUIREMENTS.md), направление в [ROADMAP.md](ROADMAP.md), технические ограничения в [AI_INTEGRATION.md](AI_INTEGRATION.md).

## D068 — Сохранять язык runtime отдельно от проекта (06.30)

- Хранить язык SDL Game UI demo в versioned user preference `game-ui-settings.cfg`, рядом с SDL preference root; не включать локаль в project files или game save slots.
- Отсутствующий файл означает English. Неизвестная версия/значение не меняют модель настроек; показать диагностику и разрешить исправление выбором языка через UI.
- При выборе языка сразу обновить интерфейс и выполнить запись через временный файл с безопасной заменой. Ошибку записи показать в Game UI с сохранением исходной ошибки ОС.
- Smoke-тест не читает/не перезаписывает реальные пользовательские настройки, а использует уникальный временный каталог, созданный самим тестом.

## D069 — Начать AI Editor API как typed in-process фасад (09.1)

- Дата: 2026-11-14.
- Решение: начать локальный API с `astro::core::AIEditorApi` поверх CommandRegistry: версия API, детерминированный каталог зарегистрированных схем и typed validate/execute.
- Фасад не дублирует исполнение: валидация, аудит, транзакции и Undo/Redo остаются в исходном CommandRegistry. Каталог отражает только команды, фактически зарегистрированные данной конфигурацией.
- Не выбирать JSON или сетевой протокол до соответствующего подэтапа и технической оценки; не считать in-process фасад security boundary. Codex adapter, разрешения, preview/approval и sandbox остаются обязательными следующими задачами.
- Обоснование: сохраняется одинаковое поведение команд и не вводится неподтверждённая зависимость или открытый endpoint.
- Подробности и ограничения: [COMMAND_API.md](COMMAND_API.md), [AI_INTEGRATION.md](AI_INTEGRATION.md), [отчёт 09.1](reports/09.1.md).

## D070 — Встроенный C# editor и пользовательские скрипты

- Дата: 2026-11-14.
- Статус: принято как требование продукта P15; Editor отсутствует, runtime host пока только технический prototype (04.5–04.6).
- В Editor должна быть отдельная встроенная Code workspace для ручного редактирования C# игровых скриптов. Скрипты сохраняются в проекте, собираются с понятной диагностикой и запускаются runtime через документированный API/ScriptBehavior.
- C# — обязательный целевой язык пользовательских скриптов; это не утверждение, что в engine уже установлен или выбран .NET/Mono/compiler/runtime host.
- Способ host/compiler, bindings, hot reload, лицензии, платформенная доступность, размер, время сборки и модель безопасности требуют отдельного технического прототипа. AI-изменения кода показываются как diff и не исполняются без предусмотренных permissions/approval; скрипты без проверенного sandbox получают обычные права процесса.
- Разделение задач закреплено в [ROADMAP.md](ROADMAP.md): 04.5 оценивает/проверяет runtime, 06.6 реализует редакторскую Code workspace после проверки основы. Требования и критерии: [PRODUCT_REQUIREMENTS.md](PRODUCT_REQUIREMENTS.md), [EDITOR_UX.md](EDITOR_UX.md), [GAME_SYSTEMS.md](GAME_SYSTEMS.md), [TESTING.md](TESTING.md).

## D071 — Кандидат C# runtime для прототипа 04.5

- Дата: 2026-11-14.
- Статус: прототип проверен на Windows x64; .NET 10 и способ hosting не утверждены для production.
- Для Windows desktop prototype оценивать .NET 10 LTS SDK **10.0.404** с официальным native hosting API (`nethost`/`hostfxr`) и framework-dependent managed component. До 04.5 SDK отсутствовал; Roslyn C# 4.14 + установленный runtime 8.0.21 выполнил только отдельный compile/run probe. .NET 8 уже завершил поддержку 2026-11-10; .NET 10 LTS поддерживается до 2028-11-14 ([политика поддержки](https://dotnet.microsoft.com/en-us/platform/support/policy), [релизы .NET 10](https://dotnet.microsoft.com/en-us/download/dotnet/10.0)).
- Основание: документация Microsoft native hosting требует C++ host, SDK headers/libraries и managed component; документированный `hostfxr`/`nethost` путь рассчитан на framework-dependent deployments ([native hosting tutorial](https://learn.microsoft.com/en-us/dotnet/core/tutorials/netcore-hosting)). Native AOT запрещает динамическую загрузку assemblies и runtime code generation, что ограничивает стратегию пользовательских динамических скриптов для mobile targets ([Native AOT limits](https://learn.microsoft.com/en-us/dotnet/core/deploying/native-aot/)).
- Источники .NET лицензируются MIT; распространяемые Windows .NET binaries подпадают под отдельные условия Microsoft .NET Library License. Перед поставкой движка и runtime пакетов требуется отдельный license/notice review ([.NET license information](https://github.com/dotnet/core/blob/main/license-information.md)).
- Риски/открытые вопросы: Android/iOS deployment and AOT, platform host adapters, runtime package size, hot reload, editor language-service integration, crash and memory boundaries, sandbox. Native in-process scripts выполняются с правами процесса и не являются sandbox.
- По явному разрешению пользователя официальный .NET 10.0.404 SDK ZIP был проверен по SHA-512 и распакован только в игнорируемый `build/04.5-dotnet-sdk`; системные PATH/registry не менялись. В CMake добавлена выключенная по умолчанию опция `ASTRO_BUILD_CSHARP_RUNTIME`; managed sample не использует NuGet-пакеты. Полный результат и ограничения: [04.5](reports/04.5-csharp-runtime-prototype.md).

## D072 — Ограниченный versioned C# ABI prototype (04.6)

- Дата: 2026-11-14.
- Статус: принят только как экспериментальный Windows x64 ABI v1; не обещает plugin compatibility и не выбирает .NET для production.
- `CSharpScriptUpdateV1` включает struct size, api version, EntityId, fixed delta и invocation-local `CSharpScriptApiV1` table. V1 callbacks читают/записывают Transform одной сущности runtime Scene; все значения проходят через `Scene::setTransform` validation.
- Принимается только точный размер struct и version 1. Callback/user-data pointer валиден только синхронно на время вызова fixed-step. Нельзя сохранять pointers, вызывать callbacks позже или считать код sandboxed.
- Сохраняемый `ScriptBehavior` отложен: нужны stable script asset IDs, schema, lifecycle, serialization/migration, diagnostics и multi-script ordering. Указатели/host runtime state не сериализуются.
- Контракт, статусы callbacks и покрытие тестами: [CSHARP_SCRIPTING_API.md](CSHARP_SCRIPTING_API.md), [отчёт 04.6](reports/04.6-versioned-csharp-runtime-api.md).

## D073 — Persistent ScriptBehavior и Scene v5 (04.7)

- Дата: 2026-11-14.
- Статус: сохранённая authoring-ссылка реализована; runtime lifecycle contract принят как целевой, его dispatch не реализован.
- ScriptBehavior v1 хранит ComponentId, Script AssetId, managed type name (UTF-8, 1–512 байт, без пробельных/control bytes) и enabled-флаг. Компонент имеет стандартную component schema version 1. Runtime/native pointers не сериализуются.
- Astro Scene v5 добавляет `astro.ScriptBehavior`. Reader продолжает принимать v1–v4, где script list считается пустым; открытие не переписывает файл, следующее сохранение пишет v5. Неизвестные типы/version/невалидные references диагностируются и не отбрасываются.
- Целевой lifecycle: resolve enabled scripts до Play; `OnCreate` один раз; `FixedUpdate` по фиксированному шагу; `OnDestroy` один раз в обратном порядке создания при Stop/rollback. Порядок — Scene entity insertion order и component insertion order. Сбой/недостающий asset должен завершать Play диагностикой, не пропускаться молча.
- На момент D073 AssetRegistry индексировал только изображения, поэтому сохранённый Script AssetId ещё не разрешался в source/assembly. D074 расширяет общий asset index для source/type/output mapping; lifecycle и Editor authoring остаются незавершёнными. Подробности: [CSHARP_SCRIPTING_API.md](CSHARP_SCRIPTING_API.md), [FILE_FORMATS.md](FILE_FORMATS.md), [отчёт 04.7](reports/04.7-persistent-script-behavior.md).

## D074 — Script assets use the shared AssetId index (04.8)

- Дата: 2026-11-14.
- Статус: принят для optional Windows C# prototype; не является финальным дизайном asset pipeline.
- `AssetRegistry` остаётся владельцем numeric AssetId: script entries используют общий счётчик с изображениями. `.astro/assets.index` v2 добавляет script metadata; v1 читается, а файл мигрирует на v2 только при явной записи регистрации/reimport.
- Script record хранит source под `Scripts/`, managed type name, output paths `.dll` и `.runtimeconfig.json` под игнорируемым `Library/Scripts/`, и source SHA-256. Разрешение проверяет границы путей, файлы и свежесть source. Повторная регистрация того же source сохраняет AssetId и обновляет mapping.
- Registry не компилирует C# и не проверяет тип самостоятельно. `CSharpScriptHost::loadScriptAsset` явно разрешает AssetId и через hostfxr ищет entry point `FixedUpdate`; имя assembly выводится из имени DLL. Это не автоматический Scene lifecycle и не sandbox.
- Покрытие: `assets.registry.integration` (совместимость индекса/пути/hash) и `runtime.csharp_host.integration` (реальное разрешение и загрузка sample assembly). См. [FILE_FORMATS.md](FILE_FORMATS.md), [ASSET_PIPELINE.md](ASSET_PIPELINE.md), [CSHARP_SCRIPTING_API.md](CSHARP_SCRIPTING_API.md) и [отчёт 04.8](reports/04.8-project-script-asset-registry.md).

## D075 — Project-local C# build manifest and immutable outputs (04.9)

- Дата: 2026-11-14.
- Статус: принят для локального Windows prototype; не утверждает .NET 10 или эту схему для production.
- Проект явно задаёт `.astro/csharp-build.json` с schema version, точной версией SDK, target framework и assembly name. Builder собирает только regular `.cs` files ниже `Scripts/`, использует пустой NuGet source list и не меняет исходники.
- Успешный build создаёт новую generation directory под ignored `Library/Scripts/<assembly>/`; предыдущие generation outputs не перезаписываются и автоматически не удаляются. `build-state.json` атомарно связывает source/config fingerprint с путями и hashes DLL/runtimeconfig; `-Check` обнаруживает source/manifest/SDK/output drift.
- Неудачная компиляция не меняет последнее успешное состояние. Build state не встраивает script mapping в AssetRegistry: вызывающий проект явно регистрирует AssetId/type/output. CLI запускает обычный пользовательский C# код с правами пользователя, поэтому не является sandbox и не должен вызываться для недоверенного кода без будущей permission/approval модели.
- Покрытие `runtime.csharp_project_build.integration`: offline build, детерминированные assembly bytes, Unicode path и UTF-8 validation, fresh/stale CLI exit codes, изменение source и immutable generation, сохранение build state при compile failure и output hash tampering. Подробности — [CSHARP_SCRIPTING_API.md](CSHARP_SCRIPTING_API.md), [FILE_FORMATS.md](FILE_FORMATS.md), [отчёт 04.9](reports/04.9-csharp-project-build.md).

## D076 — Explicit C# build-to-AssetRegistry handoff (04.10)

- Дата: 2026-11-14.
- Статус: реализован как дополнительный Windows prototype step; не утверждает финальную схему scripting pipeline.
- Проектный manifest опционально перечисляет ровно выбранные пары `sourcePath` / `managedTypeName`. Сборщик генерирует UTF-8 `Library/Scripts/asset-bindings.state` версии 1 и связывает его SHA-256 с `build-state.json` версии 2; source/type соответствие не выводится эвристикой из имён файлов.
- AssetRegistry сообщает `Missing/Current/Stale/Invalid`, проверяя handoff, точные manifest/source sets и content hashes безопасно разрешённых outputs. Явный `registerScriptsFromBuild()` валидирует все binding records до одной атомарной записи index; прежний AssetId сохраняется для того же source. Editor показывает эту диагностику и требует отдельного действия для регистрации.
- Сканирование проекта не запускает compiler, hostfxr или C# код. Managed type существование проверяется лишь при явном host load; успешная registration не означает, что CLR assembly/type успешно загрузится. RuntimeSession lifecycle, code editor, hot reload и sandbox остаются отдельными задачами.
- Обоснование: связывает проверенную project build generation с уже существующей моделью AssetId без автоматического выбора пользовательских типов и без побочных эффектов при открытии проекта.
- Проверки `assets.registry.integration`, `runtime.csharp_host.integration` и `editor.smoke`; детали формата, UX, команды и ограничения: [C# scripting API](CSHARP_SCRIPTING_API.md), [FILE_FORMATS.md](FILE_FORMATS.md) и [отчёт 04.10](reports/04.10-csharp-build-registry-diagnostics.md).

## D077 — Одноразовое согласие перед C# Play execution (04.11)

- Дата: 2026-11-14.
- Статус: политика и Editor consent gate реализованы; C# host/lifecycle всё ещё не подключены к Play.
- Editor должен до загрузки managed assembly показать прямому пользователю точный набор включённых `ScriptBehavior` в текущих project/Scene: AssetId, source, managed type, build fingerprint и hashes выходов. Подтверждение распространяется ровно на этот набор и одну Play-сессию.
- Согласие эфемерно и не сохраняется как переиспользуемое доверие в проекте или настройках пользователя. Stop, повторный Play, смена project/Scene, состава, manifest или build/source/output hash до host load сбрасывает grant. После подтверждения весь набор должен быть разрешён повторно и сравнен с показанным snapshot непосредственно перед host load. Cancel или любой missing/stale/invalid binding блокирует lifecycle callbacks для всего набора; частичный запуск не допускается.
- AI, AIEditorApi, CLI и результат сборки не выдают согласие. AI может запросить тестовый Play, но должен ждать прямого ввода пользователя в Editor. Низкоуровневый `CSharpScriptHost::loadScriptAsset` сейчас не содержит gate; будущий Editor обязан проверять grant до этого trusted in-process API.
- Обоснование: код C# загружается в процесс редактора и получает его пользовательские права. Одноразовый диалог предотвращает случайный запуск и делает согласие конкретным, но не ограничивает файлы, сеть, OS API или нативные вызовы и поэтому не является sandbox.
- В 04.12 Editor показывает project/Scene, включённые script bindings и build/output hashes; на Allow делает повторный capture и принимает только идентичный снимок. Approval хранится только в памяти и отзывается при Stop/new Play/project or Scene replacement/Faulted. `editor.csharp_play_approval.unit`, `editor.localization.unit` и `editor.smoke` покрывают модель и startup; UI interaction не автоматизировано. C# host/load/lifecycle не запускаются. Детали: [CSHARP_SCRIPTING_API.md](CSHARP_SCRIPTING_API.md), [AI_INTEGRATION.md](AI_INTEGRATION.md), [04.12 report](reports/04.12-csharp-editor-approval.md).

## D078 — C# ScriptBehavior lifecycle through RuntimeSession (04.13)

- Дата: 2026-11-15.
- Статус: реализован ограниченный Windows x64 prototype; host остаётся opt-in.
- Runtime загружает все enabled ScriptBehavior до вызова component lifecycle и сохраняет Scene insertion order. Перед каждой уникальной assembly load Editor callback повторно сверяет актуальный project/Scene/script/build snapshot. `OnCreate` выполняется перед Play, `FixedUpdate` на каждом fixed step, `OnDestroy` для созданных компонентов в обратном порядке при Stop/rollback.
- Компонентный ABI теперь требует managed `OnCreate`, `FixedUpdate`, `OnDestroy` entry points; низкоуровневый generic single-entry `CSharpScriptHost::load` оставлен совместимым. Existing users of `loadScriptAsset` must add both lifecycle methods and rebuild/register. Scene serialized schema is unchanged.
- При ошибке `OnCreate` RuntimeSession отклоняет старт и выполняет rollback; Stop всегда очищает runtime Scene, возвращая ошибку через `stopChecked`. Legacy `stop()` сохраняет исходный void/noexcept контракт.
- Обоснование: подключает сериализованный пользовательский binding к фактической Play-сессии, сохраняя one-shot approval и изоляцию authored Scene, без автоматического выбора/запуска скриптов до согласия.
- Ограничения: C# работает с полномочиями Editor process; согласие не является sandbox, module initializer может выполниться во время загрузки, unload не гарантируется; нет managed instance state, crash containment, hot reload, мобильного host или shipping review. Tests: Release full CTest 45/45; Debug without optional host 43/43. Debug C# host не линковался из-за `_ITERATOR_DEBUG_LEVEL` mismatch в локальном `libnethost.lib`.
- Подробнее: [C# scripting API](CSHARP_SCRIPTING_API.md), [AI integration](AI_INTEGRATION.md), [report 04.13](reports/04.13-csharp-runtime-lifecycle.md).

## D079 — Transactional Inspector authoring for ScriptBehavior (04.14)

- Дата: 2026-11-15.
- Status: implemented as a Windows Editor prototype.
- Inspector selects registered script bindings from the current project build; Add creates a disabled component. Enabling requires a current verified build report whose AssetId, managed type and source path still match AssetRegistry. Disabling and removal stay available to recover stale scenes. Play approval remains one-shot and the runtime gate still performs its own complete snapshot check.
- Expose add, set-enabled and remove as typed Scene commands so GUI/AI callers use the same validation, audit and transaction history. Add/remove Undo/Redo restore the exact ComponentId and component-list insertion position; Scene v5 format is unchanged.
- Rationale: support authoring existing compiled bindings without introducing an embedded source editor or silently loading/executing user code.
- Limits: Inspector smoke is programmatic; physical ImGui interaction and a current build dropdown with a live project were not automated. This UI does not edit `.cs` files, build scripts, create registry bindings, provide sandboxing, or support hot reload.
- Tests: `scene.command.integration` verifies validation, lifecycle toggling, stable-ID/order add/remove Undo/Redo; `editor.smoke` verifies Inspector presence and disabled ScriptBehavior Scene v5 save/open. Exact Debug/Release results are in [04.14 report](reports/04.14-scriptbehavior-editor-authoring.md).

## D080 — C# Code workspace baseline (04.15)

- Дата: 2026-11-15.
- Статус: реализован ограниченный Windows Editor prototype; P15 выполнен частично.
- Workspace редактирует только UTF-8 `.cs` под `Scripts/` проекта, с пределом 4 MiB на source, атомарной публикацией сохранения и явной обработкой unsaved changes при переключении документа/проекта.
- Сборка запускает уже существующий offline C# project builder только по явной кнопке и только в конфигурации Editor с настроенным локальным .NET SDK. Compiler error/warning location переводит Editor к source-файлу и строке.
- Обоснование: дать автору встроенное базовое редактирование без второй IDE/runtime зависимости и переиспользовать зафиксированный build pipeline.
- Ограничения: Windows-only, no syntax highlighting/IntelliSense/LSP/debugger/refactoring; UI-клики не автоматизированы. In-process C# остается недоверенным кодом с правами пользователя; одноразовый Play consent не sandbox. Подробности и тесты: [отчёт 04.15](reports/04.15-csharp-code-workspace.md).

## D081 — Project-owned C# build manifest settings (04.16)

- Дата: 2026-11-15.
- Статус: реализован prototype UI для существующего schema v1 `.astro/csharp-build.json`.
- Настройки принадлежат проекту: SDK pin, `net10.0`, assembly name и список source/type bindings; сохранение выполняется явной кнопкой, build pipeline использует сохранённый манифест.
- Формат строгий: неизвестные/повторные поля, неверные типы, неподдерживаемая версия, traversal и отсутствующие script sources отклоняются. Запись выполняется через временный файл и безопасную замену.
- Обоснование: дать автору настройку существующего C# build pipeline в Editor без второго формата конфигурации.
- Ограничения: Windows Editor prototype; доступны только .NET SDK 10.0.x и framework `net10.0`; другие SDK/framework выбрать нельзя. UI smoke проверяет открытие и сохранение во временном проекте программно, но не автоматизирует мышь/клавиатуру. .NET 10 не утверждён для production. Подробности: [отчёт 04.16](reports/04.16-csharp-build-manifest-editor.md).

## D082 — C# binding diagnostics must not execute project code (04.17)

- Дата: 2026-11-15.
- Статус: принят для текущего prototype.
- Project panel сравнивает явные manifest/build handoff bindings с AssetRegistry и указывает причину расхождения: отсутствующая запись, другое имя managed type, изменённый source hash или иные output paths. Явная регистрация текущей успешной сборки синхронизирует запись, сохраняя AssetId исходника.
- Обычная диагностика/открытие проекта не загружает managed assembly. Реальное наличие CLR type и ABI entry points проверяется только существующим явным Play path после consent; отдельный type probe не вводится без sandbox/permission решения, поскольку загрузка может выполнить module initializer в процессе редактора.
- Обоснование: сделать расхождения реестра понятными, не превращая refresh Project panel в скрытый запуск пользовательского кода.
- Ограничение: диагностика не доказывает наличие CLR type/методов; в текущем стеке нет проверенного статического metadata verifier. C# остаётся доверенным in-process кодом. Подробности: [отчёт 04.17](reports/04.17-csharp-binding-diagnostics.md).

## D083 — C# metadata inspection and execution isolation assessment (04.18)

- Дата: 2026-11-15.
- Статус: принято как направление проектирования; реализация и выбор/фиксация parser dependency не входят в этот подэтап.
- Не загружать пользовательские C# assemblies в Editor для Project diagnostics. Будущий verifier должен работать отдельным процессом и возвращать ограниченный структурированный результат. Предпочтителен metadata-only inspection без вызова пользовательского кода; конкретную библиотеку и версию необходимо отдельно проверить и закрепить. `MetadataLoadContext` предотвращает выполнение кода при metadata inspection, но не является sandbox; `MetadataReader` нельзя считать безопасным для произвольного недоверенного PE.
- Сам verifier должен быть изолирован средствами ОС: Windows AppContainer с минимально необходимыми read-only capabilities и без сети/профиля/секретов; ограниченные временные каталоги; строгие лимиты входа и времени. Windows Job Object рекомендуется для лимитов ресурсов и остановки всего дерева процессов, но не считается границей доступа к файлам/сети.
- Долгосрочный C# runtime для недоверенных проектных скриптов должен работать вне Editor в отдельном ограниченном процессе через узкий версионируемый IPC. Действующий ABI v1 и in-process host остаются совместимыми и предназначены только для явно доверенного локального prototype. Миграция на новый ABI требует отдельного opt-in плана; автоматической миграции нет.
- Consent gate не превращает скрипт в sandbox. AI/Codex может запросить запуск, но не может выдать пользовательское согласие или предоставить worker дополнительные capabilities.
- Будущая приёмка включает отсутствие исполнения module initializer при инспекции, устойчивость Editor к malformed/oversized input, проверку AppContainer deny/allow policy, лимиты/kill worker и совместимость текущего host. См. [оценку 04.18](reports/04.18-csharp-metadata-isolation-assessment.md).

## D084 — Windows C# metadata-validator worker prototype (04.19)

- Дата: 2026-11-15.
- Статус: реализован ограниченный Windows x64 прототип; не подключён к Editor и не является обещанием безопасного запуска недоверенного C#.
- Для чтения CLR metadata использовать `System.Reflection.Metadata` из целевого .NET 10 shared framework без отдельной NuGet-зависимости. Проверка принимает один PE до 64 MiB, ищет явный managed type и сверяет три ABI v1 entry points и `UnmanagedCallersOnlyAttribute` без загрузки assembly и создания атрибутов.
- Парсер запускается в отдельном Windows AppContainer worker без сетевых capabilities. На один запрос создаётся временная рабочая область с копией runtime/worker и ровно выбранной DLL; разрешения AppContainer выдаются только на эту область. Временный профиль удаляется после запуска. Environment block worker ограничен allowlist системных путей и .NET переменных, а не наследуется от Editor.
- Worker ограничен 16 KiB request, 32 KiB response, 15 s operation timeout и 512 MiB process-memory Job Object; timeout probe доказывает остановку worker. Запуск Windows x64 и AppContainer deny file/network проверяются integration-тестом. `MetadataReader` по документации Microsoft не предназначен для недоверенного input, поэтому защита всё ещё зависит от Windows isolation и не доказывает отсутствие иных OS/runtime attack surfaces.
- Никакой Project panel/AI API не вызывает worker в 04.19. C# in-process host ABI v1 не менялся и остаётся только для доверенных локальных прототипов; миграции нет.
- Подробности, команды и фактические ограничения: [отчёт 04.19](reports/04.19-csharp-metadata-validator-worker.md).

## D085 — Project panel C# metadata diagnostics (04.20)

- Дата: 2026-11-15.
- Статус: реализовано как явная Windows x64 диагностика; C# Play host ABI v1 не меняется.
- Проверка выполняется только по кнопке, только для свежей сборки и текущих зарегистрированных AssetRegistry bindings. Editor не загружает пользовательскую DLL: запускается существующий metadata worker вне процесса Editor; результат читается асинхронно.
- Native runner принимает только нормализованный относительный `Library/Scripts/*.dll`, проверяет reparse points и канонический путь под project root, передаёт только текущие registered managed types. C# сборка недоступна одновременно с worker, чтобы файлы output не менялись во время staging.
- Worker API принимает максимум 16 типов за одну изолированную сессию; для больших проектов Editor разбивает список на пакеты. Копирование runtime для каждого пакета может быть медленным. Совокупный вывод ограничен 32 KiB, усечение указывается.
- Ограничения: Windows-only; metadata parser не сертифицирован для враждебных PE; AppContainer проверен на текущем хосте, но не является доказательством отсутствия иных атак; Play host остаётся trusted-code-only in-process.
- Тесты, команды и результаты: [отчёт 04.20](reports/04.20-csharp-project-metadata-diagnostics.md).

## D086 — C# metadata validation batch limits (04.21)

- Дата: 2026-11-15.
- Статус: реализовано для Windows x64; runtime ABI, sandbox, AppContainer ACL и runtime staging не менялись.
- Один worker может проверять до 64 registered managed types при общем UTF-8 лимите имён 8 KiB; протокольные request/response остаются в пределах 16/32 KiB, type count проекта ограничен 256. Editor делит план по обоим лимитам.
- Общий Editor report cap установлен в 128 KiB, worker response cap — по-прежнему 32 KiB. При потере содержимого вывод содержит явную пометку. Панель показывает число запрошенных типов и isolated batches.
- На локальном Windows Release synthetic ABI fixture один baseline-прогон четырёх последовательных 16-type сессий занял 5.48 s; после изменения медиана трёх 64-type прогонов составила 1.35 s. Типовая форма соответствует C# ABI, но не включает зависимости/объём реального пользовательского проекта; измерение не является общим benchmark для других устройств.
- Отдельный shared runtime cache не добавлен: измеренная batching правка сокращает типичный overhead без постоянных файлов или расширения ACL/AppContainer доступа. Реальный проект следует профилировать отдельно перед cache-дизайном.
- Подробности, тесты и логи: [отчёт 04.21](reports/04.21-csharp-metadata-batch-performance.md).

## D087 — Профиль C# metadata и UX большого отчёта (04.22)

- Дата: 2026-11-15.
- Доступная в репозитории сборка `AstroEngine.CSharpSample` содержит только один managed type. Её пять Release замеров (медиана 1,456 с, ответ 282 байта) полезны как baseline этой машины, но не обосновывают latency для большого игрового проекта.
- На машине найден отдельный Unity 6.5.2 проект; его cached `Assembly-CSharp.dll` stale для одного исходника и его `MonoBehaviour` не соответствует текущему AstroEngine host ABI. Validator прогоны отклонены ожидаемо; считать их успешной profile/compatibility проверкой нельзя. Не запускать Unity batch build в рамках этого этапа, поскольку он меняет generated Library project state. Абсолютный локальный путь не записывать в репозиторные docs.
- Smoke создаёт только в `--smoke` синтетический текстовый отчёт 128 KiB; тест фиксирует прокрутку и время отрисовки текста (<250 мс). Это проверка производительности виджета, не визуальное UX-исследование с реальными диагностическими строками.
- Не менять лимиты batch/report, не добавлять долгоживущий staging/runtime cache, не ослаблять AppContainer и не менять ABI по результатам 04.22: найденная Unity DLL мала, устарела для одного script и оба requested bindings отклонены. Это подтверждает лишь время обработки неподдерживаемого input. Текущий AstroEngine C# host не обещает Unity MonoBehaviour compatibility; такой adapter потребует отдельного product/API/security design и явного подэтапа.
- Полный CTest на текущем Windows x64: Debug 46/46, Release+C# 49/49. Детали: [отчёт 04.22](reports/04.22-csharp-metadata-project-ux.md).

## D088 — Runtime Game UI host content for Puzzle demo (05.7)

- Дата: 2026-11-15.
- `ImGuiGameUiAdapter` exposes an optional host-owned gameplay renderer callback. The Game UI model remains backend-neutral; the SDL demo owns `PuzzleGame` and draws its board in the Gameplay body.
- Keep the demo board fixed at a deterministic 8×8 seed for reproducible smoke checks. Reuse `PuzzleGame::swap`, the existing HUD model, and RuntimeSession transitions; do not duplicate puzzle rules in UI code.
- Build `astro_runtime_puzzle` when the optional Game UI demo is enabled, even with `BUILD_TESTING=OFF`, so packaged runtime demo has its gameplay dependency without making Puzzle mandatory for the headless engine.
- This is an executable sample integration only. It does not satisfy multi-level campaign, persistent progression, physical-input automation, board visual golden, or clean packaged ZIP acceptance. No current released ZIP was replaced.
- Results and limitations: [05.7 report](reports/05.7-puzzle-game-ui.md).

## D089 — Standalone runtime host for authored Puzzle campaigns (05.8)

- Date: 2026-11-15.
- The optional SDL Game UI demo accepts an explicit project root and campaign ID and loads through `PuzzleCampaignSession`; the existing campaign parser/resolver/session remain the single source of campaign behavior.
- Apply submitted tile swaps through `RuntimeSession` fixed-step callbacks. A reached score target enables one explicit Continue action. Each action resolves one transition and loads the selected level; terminal `END` retains the final level and score until the user returns to the menu.
- Preserve the no-argument seeded Puzzle demo and headless/default optional-module policy. Windows command-line paths are converted from wide arguments to UTF-8 and then to `std::filesystem::path`.
- Add a small repository sample project and read-only end-to-end smoke. Campaign progress saves, auto-advance, external project packaging and physical input automation remain outside this scope.
- Results and limitations: [05.8 report](reports/05.8-runtime-puzzle-campaign.md).

## D090 — Explicit campaign progress slots in standalone Game UI (05.9)

- Date: 2026-11-15.
- Reuse SaveStore's versioned slot format and integrity checks for authored Puzzle campaign progress; do not introduce a parallel campaign file format.
- Persist only explicit custom fields: campaign ID, active level ID, terminal END flag, and deterministic Puzzle snapshot. The sample selects no Scene properties and performs no automatic writes.
- Store slots in SDL user data under a stable FNV-1a key derived from the canonical project path, followed by campaign ID. Keep player progress outside project content.
- Validate profile/game version, campaign identity, listed level ID, snapshot compatibility with current authored rules, and END resolution before publishing the restored candidate Scene/session. Failed loads are non-mutating.
- Scope is the standalone SDL Game UI only. Editor Game View slots, migrations, autosave, and slot previews remain future work.
- Results and limitations: [05.9 report](reports/05.9-campaign-progress-save.md).

## D091 — Refresh the Windows runtime package after 05.9

- Date: 2026-11-15.
- Rebuild the existing opt-in CPack ZIP from current sources after adding Puzzle/campaign gameplay and manual campaign progress. Keep the established package contents (standalone demo, runtime README, SDL3 and Dear ImGui license texts); do not add authored project files, Editor, or build tools.
- Generate into a new build directory so the earlier `build/05.6-runtime-release` archive remains untouched.
- Validate both the registered package smoke (fresh extraction and `--smoke`) and direct smoke from the exact generated archive, including the sample campaign's save/restore path.
- This reissues a runtime demo package; it is not full-game export or Stage 05 completion. See [package refresh report](reports/05.6-package-refresh-after-05.9.md).

## D092 — Bundle the sample campaign with the Windows runtime package (05.10)

- Date: 2026-11-15.
- Include only the checked-in two-level Puzzle campaign in the opt-in demo ZIP, under the relative ASCII path `game-data/puzzle-campaign`; preserve the source sample unchanged.
- Include `launch_puzzle_campaign.cmd`; resolve the executable and project root through `%~dp0` so launch behavior does not depend on the caller's working directory.
- Keep player progress in SDL user data outside the archive. Do not imply general project export, Editor packaging, or completed Stage 05 acceptance.
- Automated package smoke invokes the executable directly with bundled data; separately verify the `.cmd` launcher manually because CMake `execute_process`/`cmd.exe` quoting was not reliable in this environment.
- Results and limitations: [05.10 report](reports/05.10-self-contained-campaign-package.md).

## D093 — Editor Game View campaign progress slots (06.31)

- Date: 2026-11-15.
- Reuse `PuzzleCampaignSaveAdapter` and the existing versioned SaveStore format for Editor Game View; do not create a second campaign save format.
- Opt in only to campaign ID, active level ID, terminal END, and Puzzle snapshot. The profile selects no Scene properties or unrelated game state.
- Keep player data outside project content under the configured SDL user-data subdirectory, then `PuzzleCampaigns/<campaign-id>`. Offer ten explicit manual slots (0–9).
- Load through the existing validated candidate path and replace the active campaign session only after all slot and authored-level checks pass.
- Autosave, slot previews, migrations, and arbitrary game-state persistence remain out of scope.
- Results and limitations: [06.31 report](reports/06.31-editor-campaign-progress.md).

## D094 — Give Editor Game View campaign controls usable first-use space (06.32)

- Date: 2026-11-15.
- Give the ordinary interactive Game View a 520×560 first-use window so the campaign board and explicit save controls have usable initial space.
- Keep slot selection on its own row and place Save/Load actions on the following row; do not require horizontal scrolling for primary actions.
- Do not apply the interactive window-size override in screenshot smoke or benchmark runs, preserving their established graphics capture geometry.
- This is a layout decision only; SaveStore format, campaign data and slot policy remain those of D093. Results: [06.32 report](reports/06.32-editor-campaign-save-ui.md).

## D095 — Keep campaign save namespaces stable and never migrate implicitly (06.33)

- Date: 2026-11-15.
- Resolve the Editor path as SDL user-data root + the relative `userDataSubdirectory` stored in `.astro/puzzle-save.settings` + `PuzzleCampaigns/<campaign-id>`.
- A campaign ID change selects another directory; leave the previous slots untouched. Do not rewrite saved campaign identity or silently copy slots.
- A project move or copy that carries the settings sidecar keeps its relative save namespace. Copies therefore share slots until the user explicitly configures another subdirectory. Projects without a settings sidecar continue to derive a default subdirectory from the current canonical project path.
- Do not implement automatic migration. A future copy/move UI must preview source/target, preserve source data, reject collisions, and require explicit approval before copying or replacing files.
- The resolver validates the relative directory and campaign ID but performs no directory creation or file I/O. Tests use only the Editor smoke's temporary fixture. See [06.33 report](reports/06.33-campaign-save-identity.md).

## D096 — Do not copy campaign saves across campaign identities (06.34)

- Date: 2026-11-15.
- The current progress payload stores `puzzle.campaign.id`; `loadPuzzleCampaignProgress` rejects it when it differs from the requested campaign ID. A file-only copy cannot migrate a renamed campaign safely.
- Any future initial transfer support is limited to the same campaign ID and must begin with a read-only preview of slots 0–9, validation/recovery status, destination collisions and total bytes. The preview itself must not create directories or alter files.
- A later write operation must be copy-only, preserve every source artifact, refuse destination collisions by default, recheck the preview immediately before writing, and use the existing atomic-save guarantees. Payload identity/schema transformation requires a distinct migration design and tests.
- No transfer or save migration was implemented in 06.34. User-data writes require separate explicit confirmation after reviewing a concrete preview. See [06.34 assessment](reports/06.34-campaign-save-transfer-assessment.md).

## D097 — Keep the first campaign transfer preview read-only and same-ID (06.35)

- Date: 2026-11-15.
- Add a runtime API that inspects only slots 0–9 and requires the expected campaign ID. It reports empty, valid, backup-recovered, invalid and mismatched slots, expected-file byte counts, primary/backup destination collisions, and unexpected source artifact names.
- Preview must not create directories, copy files, or rewrite payloads. Use `SaveStore::inspectExistingSlot` to inspect an existing source slot without exposing a writable store, including the validated primary or recovered backup.
- Reject parent traversal and symlink/reparse points in source/target directory paths and expected slot paths.
- This preview checks save-format integrity and the embedded identity/required-field types. It does not validate a saved Puzzle snapshot against current authored campaign levels; the normal load path remains the compatibility check.
- File transfer, repair of corrupt primaries, GUI presentation and any user-data writes remain separate work. Results and limits: [06.35 report](reports/06.35-campaign-save-transfer-preview.md).

## D098 — Show campaign save transfer preview in Editor, read-only (06.36)

- Date: 2026-11-15.
- `Puzzle Save Settings` may call the 06.35 same-ID preview using the applied source settings and destination directory currently in the edit field. The preview is cleared whenever that destination changes.
- The preview must not persist settings, create a target directory, copy/repair/delete files, or alter save payloads. Resolving the Windows app data root reads `%APPDATA%`; it must not call APIs that implicitly create the application data directory.
- Smoke uses a temporary fixture and asserts the UI preview path is reached, the absent destination remains absent, and source slot bytes are unchanged.
- Show per-slot format status, recovery state, bytes, collision status and unexpected artifact names. Warn that this does not validate Puzzle snapshot compatibility against current authored campaign rules.
- A future transfer action requires its own substage and concrete preview revalidation before writes. See [06.36 report](reports/06.36-campaign-save-transfer-editor-preview.md).

## D099 — Revalidate campaign save transfer preview before write (06.37)

- Date: 2026-11-15.
- A future copy action must rerun inspection immediately before writing and must not use a displayed preview as authority by itself. The revalidation API added in 06.37 performs no writes and reports `Ready`, `StaleSource`, `StaleDestination`, `DestinationConflict`, or `SourceNotTransferable`.
- Source freshness compares slot state, identity, exact byte totals, primary/backup presence and bounded content fingerprints. FNV-1a detects ordinary stale changes; it is not a cryptographic integrity check or security boundary.
- Any expected destination primary or `.bak` path with an existing filesystem entry is a conflict, including zero-byte files and directories. The default policy is refusal; overwrite and merge behavior are not specified.
- Invalid, mismatched, oversized or incompletely fingerprinted source slots and unexpected source artifacts block `Ready`. Empty numbered slots are allowed, but there must be at least one source slot to copy.
- Any future writer is copy-only, preserves source bytes, revalidates immediately before execution, uses exclusive target creation, and requires explicit user confirmation for that operation. Atomic/partial-failure cleanup semantics must be designed and tested before implementation.
- No file transfer, settings mutation, target directory creation or migration was added in 06.37. See [06.37 report](reports/06.37-campaign-save-transfer-preflight.md).

## D100 — Same-ID copy-only campaign save transaction (06.38)

- Date: 2026-11-15.
- Runtime may copy only recognized primary and `.bak` files from a `Ready` 06.37 preflight. Copy bytes verbatim; do not rewrite payload identity/schema. Invalid, mismatched, oversized, unclassified or unexpected source artifacts remain ineligible.
- Require a caller-supplied explicit-confirmation flag for every invocation that can write. This flag is not an Editor approval system; the 06.38 API is not connected to a user-facing button.
- Create each target file exclusively (`CREATE_NEW` on Windows, `O_EXCL` on POSIX). Never replace or merge an existing target path. Revalidate before creating target directories, then rely on exclusive file creation to reject races during the batch.
- On cancellation or ordinary I/O failure, remove only exact created outputs that still match this attempt and remove only empty directories created by this attempt. Preserve competing/pre-existing files. Report remaining artifacts if rollback is incomplete.
- This is rollback on detected failure, not crash-atomic multi-file commit. A process/power failure may leave partial target files; a future UI must disclose that and offer diagnostics. Source files remain untouched.
- No real user save directory was used. Unit/integration coverage uses isolated temporary folders. See [06.38 report](reports/06.38-campaign-save-copy-transaction.md).

## D101 — Editor confirmation before campaign save copy (06.39)

- Date: 2026-11-15.
- A preview alone never copies data. The Editor requires a separate confirmation dialog for each operation and shows canonical source/target paths, eligible slot count, byte total and no-overwrite policy before the confirm action.
- The confirm action invokes the 06.38 copy API, which revalidates immediately before writes. Editor reports copy state/count/diagnostic and every remaining artifact when rollback is incomplete.
- The Editor smoke invokes that same confirmation action only against a unique temporary fixture and verifies the source remains and the copied slot bytes match. It does not exercise real AppData saves.
- Same-ID copy only; no payload migration, merge or overwrite. Process/power interruption may still leave partial files, as the transaction has no crash-recovery journal. See [06.39 report](reports/06.39-campaign-save-transfer-editor-confirmation.md).

## D102 — Journal and report campaign save transfer interruption (06.40)

- Date: 2026-11-15.
- Before copying any slot artifacts, create an exclusive bounded `.astro-campaign-transfer.pending` journal containing the safe campaign ID and the planned slot/kind/size/fingerprint list. Flush it before the first output. Normal success and handled rollback remove only the exact journal bytes created by that operation.
- A later preview reports the journal and classifies each expected target artifact as `Complete`, `Partial`, `Missing`, `Changed`, or `Unsafe`. Any pending journal blocks another transfer. Invalid journals are reported and cannot be acknowledged by the Editor API.
- Editor acknowledgement requires a separate confirmation and removes only an unchanged valid journal after re-inspecting the listed artifacts. It never deletes, repairs or overwrites campaign save files. Partial/changed files require manual review; after acknowledgement, ordinary destination conflict checks still protect them from overwrite.
- Integration coverage terminates a child test process after its first copied artifact, then inspects the state from the parent process using Unicode temporary paths. The test is a process-interruption test; it does not simulate power loss or guarantee directory-entry durability on all filesystems. No real project/user data is used. See [06.40 report](reports/06.40-campaign-save-transfer-recovery-journal.md).

## D103 — Explicit resume for campaign save transfers (06.41)

- Date: 2026-11-15.
- A valid interrupted transfer may be resumed only through a separate explicit-confirmation API/UI action. Existing copy behavior remains unchanged and stays blocked when a recovery journal exists.
- Immediately before resume, re-read and compare journal identity/fingerprint/artifact states, compare the displayed source preview with fresh source inventory, and require the journal's slot/kind/size/fingerprint inventory to match the current source. Source location itself is not embedded in journal v1; this is content/inventory matching, not provenance authentication.
- Skip only exact `Complete` target artifacts. Create only `Missing` artifacts with exclusive creation. `Partial`, `Changed`, `Unsafe`, changed-after-review artifacts, changed sources, and racing destination files block/abort. Never repair or overwrite a target artifact.
- On detected error/cancellation, rollback only exact outputs created by this resume invocation and retain the journal. Remove the unchanged journal only after all listed artifacts inspect as complete. Resume does not provide cross-process locking, power-loss durability, automatic cleanup, or cryptographic integrity.
- Integration coverage uses isolated Unicode temporary fixtures and child-process interruption; Editor smoke checks the recovery UI offers the resume control. No real project saves are touched. See [06.41 report](reports/06.41-campaign-save-transfer-resume.md).

## D104 — Preserve partial campaign save targets (06.42)

- Date: 2026-11-15.
- A target artifact classified `Partial` remains a conflict. The resume API must not delete, truncate, append to, repair, or overwrite it; it returns `DestinationConflict` and retains the recovery journal.
- The Editor must identify partial artifact paths/status and disable resume while any partial artifact exists. The user must inspect it manually before taking any separate cleanup action.
- Journal v1 stores only the complete artifact byte count/fingerprint; it does not bind a partial file's observed prefix to the transfer attempt. Matching source-prefix bytes are not proof of provenance. Therefore no automatic partial recovery is considered safe, and no journal format migration is introduced here.
- Integration coverage constructs a shorter target file after child-process interruption and verifies the resume refusal leaves its exact bytes, the missing companion file and the journal unchanged. Editor smoke checks a partial fixture displays the warning and non-resumable state. No real save path is used. See [06.42 report](reports/06.42-partial-campaign-save-transfer-policy.md).

## D105 — Diagnose invalid campaign transfer journals (06.43)

- Date: 2026-11-15.
- Recovery inspection reports a typed journal state: `Unreadable`, `Unsafe`, `Oversized` or `Malformed`. Successfully parsed records use `Valid`; absent records use `Missing`.
- Invalid journal data never supplies inferred target artifact paths. Copy and resume stay blocked by recovery-required state; acknowledgement refuses invalid records. Editor displays the category and parser/read diagnostic and instructs manual inspection without changing save files.
- Tests cover an unsupported version, truncated v1 body, over-limit journal, and a directory occupying the journal path. They assert no candidate slot path is inferred and preserve the journal/path occupant and a slot-looking file. No real project data is used. See [06.43 report](reports/06.43-invalid-campaign-transfer-journal-diagnostics.md).

## D106 — Archive readable malformed campaign journals (06.44)

- Date: 2026-11-15.
- Only a bounded, readable record classified `Malformed` is eligible for the in-Editor archive operation. Require a separate explicit confirmation.
- Copy the exact reviewed bytes to a deterministic adjacent archive path using exclusive creation. If that path exists, accept it only when it is a regular readable file with identical bytes; never replace a different occupant.
- Historical 06.44 behavior attempted to retire the source after a verified archive. The 06.45 review supersedes that part; see D107. The current flow never removes the malformed source journal.
- Slot artifacts are never inferred, opened, or changed. `Unreadable`, `Unsafe` and `Oversized` journals remain manual-review cases. No journal format migration is added. See [06.44 report](reports/06.44-malformed-campaign-journal-archive.md) and [06.45 report](reports/06.45-race-safe-malformed-journal-archive.md).

## D107 — Preserve malformed journals during archive (06.45)

- Date: 2026-11-15.
- A final path reread followed by `std::filesystem::remove(path)` is a TOCTOU window: another process may replace the path between validation and deletion. Portable C++ filesystem APIs do not provide an atomic compare-and-delete operation.
- The confirmed action now exclusively creates an adjacent exact-byte archive copy, verifies it, and leaves the original malformed journal in place. The journal continues to block transfer/resume until a person reviews it. Identical archive retries are idempotent; different existing contents are preserved and cause failure.
- At 06.45 the malformed archive flow had no cross-platform lifecycle lock or multiprocess race coverage. 06.48 adds a tested source-journal lease but still does not retire or delete the original malformed journal. See [06.45 report](reports/06.45-race-safe-malformed-journal-archive.md) and [D111](DECISIONS.md#d111--lock-malformed-journal-archive-0648).

## D108 — Handle-based valid recovery journal acknowledgement (06.46)

- Date: 2026-11-15.
- On Windows, acknowledgement opens the reviewed journal with `GENERIC_READ | DELETE`, denies new write/delete sharing, reads and compares the bounded exact bytes through that same handle, then marks that handle for deletion with `SetFileInformationByHandle(FileDispositionInfo)`. If the file is busy, replaced, reparse-backed, changed or the filesystem refuses disposition, the API fails and leaves the journal in place. It never falls back to `std::filesystem::remove(path)`.
- The caller must still provide explicit confirmation and the current artifact list must match the displayed recovery review. Save artifacts are not removed, rewritten or made overwriteable by acknowledgement.
- At the end of 06.46 POSIX acknowledgement still failed closed; that platform gate was satisfied in 06.47 and the current implementation is documented in D110. The original requirement was `O_NOFOLLOW`, advisory `flock`, open-inode comparison with `fstatat(..., AT_SYMLINK_NOFOLLOW)` on a held directory descriptor, and `unlinkat`. Advisory locks coordinate only cooperating processes; they do not defend against arbitrary local writers that ignore the lock.
- Windows integration tests use isolated Unicode temp directories and verify stale-preview refusal, refusal while a competing writer handle is open, successful handle-based acknowledgement, and save-artifact preservation. These tests do not claim protection from privileged kernel/filesystem actors or power loss. See [06.46 report](reports/06.46-handle-based-campaign-journal-acknowledgement.md).

## D109 — Gate POSIX journal acknowledgement on a runnable target (06.47 assessment)

- Date: 2026-11-15.
- Historical gate: do not enable POSIX acknowledgement before a runnable POSIX build and integration/race tests exist.
- This gate was satisfied during 06.47 after user authorization to restart Windows, install Ubuntu 24.04/WSL2 and install local build tools in that distribution. See [06.47 report](reports/06.47-posix-journal-acknowledgement-readiness.md).

## D110 — Cross-platform campaign journal lifecycle lock (06.47)

- Date: 2026-11-16.
- Copy holds the newly created journal lease until commit/rollback; resume holds the existing journal lease through its final artifact verification; acknowledgement validates the displayed preview, takes the lease, rereads exact bytes and removes only that opened file object/verified directory entry. This prevents cooperating transfer/acknowledgement operations from retiring a journal that marks an in-flight transaction.
- Windows uses an exclusive `LockFileEx` byte-range lock and `SetFileInformationByHandle` for removal. The lock permits ordinary open/read handles where Windows share semantics allow them; operations that conflict with a locked byte range fail closed. POSIX uses `openat` with `O_NOFOLLOW`, `flock(LOCK_EX | LOCK_NB)`, `fstatat(AT_SYMLINK_NOFOLLOW)` identity comparison, and `unlinkat` relative to an open directory descriptor.
- POSIX `flock` is advisory: it coordinates only cooperating processes. The directory-entry identity check narrows replacement races but cannot make `fstatat` plus `unlinkat` atomic against hostile non-cooperating local processes. These functions are not a security boundary against privileged/local attackers, nor a power-loss durability guarantee.
- Isolated integration tests cover an active copy, acknowledgement contention from another process on POSIX and a competing range lock on Windows, successful acknowledgement, stale bytes and preservation of slot artifacts. Full Release CTest passed 39/39 on WSL Ubuntu 24.04 and 52/52 on Windows. See [06.47 report](reports/06.47-posix-journal-acknowledgement-readiness.md).

## D111 — Lock malformed journal archival (06.48)

- Date: 2026-11-16.
- Keep the existing explicit confirmation, bounded `Malformed` eligibility, deterministic archive path, exact-byte verification and no-overwrite behavior. Retain the source malformed journal; 06.48 does not add automatic repair, retirement or deletion.
- Perform read-only path classification before taking the lease, because Windows `LockFileEx` locks the journal byte range against second-handle reads. Then acquire the same lifecycle lease used by copy/resume/acknowledgement, compare the bounded exact bytes through the held source handle, create or verify the archive, and reread the source through that held handle before success.
- If another cooperating operation owns the lease, fail before archive creation and preserve source/archive/save artifacts. POSIX `flock` remains advisory; it cannot prevent a non-cooperating process from changing or replacing the journal. No save slot files are accessed or changed.
- Integration tests verify contention by a separate POSIX process and a Windows range lock, no archive creation while locked, source/save preservation, and normal/repeated archive success after lock release. Full Release CTest: POSIX 39/39; Windows 52/52. See [06.48 report](reports/06.48-malformed-journal-archive-lock.md).

## D112 — Preserve partial journal archive residue (06.49 assessment, superseded by D113)

- Date: 2026-11-16.
- Current archive creation writes directly to its final fingerprint-derived path using exclusive creation. If the process exits after creating that path but before all bytes are written, the partial file remains. A retry sees an existing non-identical archive and fails closed; it does not overwrite or remove it. The malformed source journal remains the recovery source and continues blocking save transfer operations.
- Do not auto-delete or resume a partial archive: the application cannot prove that a mismatching occupant is its own interrupted output rather than user data. Manual inspection is required. The integration test models this state with a truncated file in an isolated temp fixture; it is not a real power-loss/process-termination test.
- The implementation candidate was completed in 06.50; D113 defines current behavior. The historical 06.49 fixture remains useful for verifying fail-closed handling of a pre-existing partial final archive. See [06.49 assessment](reports/06.49-partial-journal-archive-assessment.md).

## D113 — Staged atomic no-replace journal archive publication (06.50)

- Date: 2026-11-16.
- Keep the explicit confirmation, source journal lease, exact reviewed bytes, deterministic final archive name and no-overwrite rule. Write the archive to a unique same-directory staging file, flush it, then verify the exact bytes through the held file handle before publication.
- Windows publishes the open file with `SetFileInformationByHandle(FileRenameInfo)` and `ReplaceIfExists=FALSE`. POSIX verifies the staging directory entry still names the held inode, publishes with same-directory `linkat` (which fails if the final path exists), verifies the final inode, attempts file/directory `fsync`, then unlinks only the verified staging entry. No overwrite-capable fallback is permitted.
- If publication fails before commit, remove the staging entry only through its held lease and only if exact contents and object identity still match. If the process terminates, its staging entry may remain; future attempts use unique names and do not auto-delete unidentified remnants. A pre-existing partial or conflicting final archive remains fail-closed and requires manual review. The original malformed journal and save slots remain untouched.
- Integration tests cover stale staging preservation/retry, exact archive bytes, existing partial-final refusal, collisions, source lease contention and save/source preservation on Windows and POSIX. Tests do not simulate kernel crash or power loss. Windows directory-entry durability is delegated to the filesystem; POSIX directory `fsync` is attempted, but filesystem guarantees vary. See [06.50 report](reports/06.50-staged-journal-archive.md).

## D114 — Subprocess interruption tests for staged journal archive (06.51)

- Date: 2026-11-16.
- Exercise process termination at three deterministic points: after a partial staging write, after exact-byte verification and flush but before publish, and after publish but before staging cleanup. A child process performs the real archive operation; the parent terminates it after a private callback signals the selected boundary.
- Keep the hook in a private runtime test header and invoke it only through an internal test entry point. The shipped archive API accepts no hook parameter and reads no environment variable. Test pauses are controlled by the isolated integration harness and bounded by a timeout.
- Expected states: before publication, final archive is absent and partial/complete staging residue remains; retry publishes the exact final copy without changing old residue. After publication, final bytes are exact and retry is idempotent; POSIX may retain the staging hard link while Windows has renamed it. In every case the malformed journal and candidate slot bytes remain unchanged.
- The test uses `TerminateProcess` on Windows and `SIGKILL` on POSIX. It proves process-interruption behavior on the tested filesystems, not kernel crash, power-loss durability, controller write-cache behavior, or arbitrary hostile concurrent path replacement. Full Release CTest: Windows 40/40, WSL Ubuntu 39/39. See [06.51 report](reports/06.51-archive-interruption-tests.md).

## D115 — Manual review of legacy conflicting archive files (06.52 assessment)

- Date: 2026-11-16.
- Reuse the deterministic archive path only when it is a readable regular file containing the exact reviewed journal bytes. Keep all differing, unreadable, unsafe, directory and reparse-point occupants fail-closed and untouched.
- Do not infer that a short file or byte-prefix match is an interrupted archive. Such a file could be unrelated user data. The archive name contains a 64-bit non-cryptographic change fingerprint; it is a lookup key, not proof of authorship or uniqueness.
- A future diagnostics UI may report the expected path, no-follow file kind, size, readability and bounded byte-comparison category (exact, different, prefix match, unavailable). Any prefix match must be described only as observed bytes, not as verified archive provenance. Keep the malformed source journal and save slots unchanged.
- Recovery for an unknown occupant remains a manual operator task outside the engine. Do not add automatic delete, move, truncate, overwrite or cleanup. No code/API/UI behavior changed in 06.52. Baseline Release CTest passed Windows 40/40 and WSL Ubuntu 39/39. See [06.52 assessment](reports/06.52-legacy-archive-conflict-assessment.md).

## D116 — Read-only diagnostics for archive conflicts (06.53)

- Add a runtime API that inspects only the deterministic archive path associated with a displayed, revalidated malformed journal. Preserve existing archive naming and write behavior.
- Open without following a Windows reparse point or POSIX symbolic link. Classify missing, regular file, directory, link/reparse point, other and unavailable. Read at most the existing 4096-byte journal bound and compare bytes as exact, different, prefix, oversized or unavailable. Recheck the held object's identity/metadata before returning a content comparison.
- Surface the expected path, object type, observed size, comparison, and platform diagnostic in Editor after archive refusal. A prefix describes bytes only and does not establish authorship or prove an interrupted AstroEngine write.
- Inspection never creates, deletes, moves, truncates, replaces or repairs an archive, source journal or save slot. No format/API break or external dependency is introduced.
- Windows Release full CTest: 44/44 (including Editor smoke); WSL Ubuntu Release: 39/39. POSIX link creation is permission-dependent in tests; it passed on the current WSL filesystem. Kernel/power-loss behavior and arbitrary concurrent hostile path replacement are not claimed. See [06.53 report](reports/06.53-archive-conflict-diagnostics.md).

## D117 — Bounded explicit copy of unknown archive occupants (06.54 assessment)

- A future UI may offer a separate, explicit **copy-aside** action only for a readable regular file at the reviewed deterministic archive path, bounded by the existing 4096-byte limit. This is a proposal; 06.54 adds no runtime behavior.
- The operation must no-follow the final source object, retain/revalidate the same source handle while reading exact bytes, and publish a new adjacent copy under a generated distinct name with atomic no-replace semantics. If the source changes or the destination collides, abort and preserve both.
- Require explicit confirmation showing source path, destination path, byte count and a statement that the occupant's creator is unknown. Do not display or infer sensitive file contents. The copied file is an unverified evidence copy, not a validated journal/archive.
- Keep the original conflict and source recovery journal untouched; successful copy-aside does not clear the deterministic path or trigger a retry. Links/reparse points, directories, other object types, unreadable and oversized files are ineligible. Resolution remains manual.
- If implemented later, test cancel/no-write, exact bytes, source/slot preservation, changed/replaced source, destination collision, symbolic link/reparse point, size boundary, Unicode path, and child-process interruption before/after atomic publication on Windows and POSIX. No additional dependency is justified.
- 06.54 was a documentation-only assessment. Pre-edit Release CTest passed Windows 44/44 and WSL Ubuntu 39/39. See [06.54 report](reports/06.54-conflict-copy-assessment.md).

## D118 — Explicit bounded copy-aside for archive conflicts (06.55)

- Date: 2026-11-16.
- Offer copy-aside only after read-only diagnostics identify a differing readable regular file at the deterministic archive path. Limit source size to 4096 bytes; reject links/reparse points, directories, unsupported kinds, unavailable and oversized objects.
- Preview captures the exact bytes and object identity without writing. The Editor confirmation shows exact source/destination paths, byte count and states that author/provenance is unknown and the result is unverified. Cancel does not write.
- On confirmation, reacquire and hold the source no-follow lease, verify object identity and exact bytes against preview, revalidate the malformed recovery journal, and create a unique adjacent staging file. Publish to the reviewed generated adjacent name atomically without replacement. Any stale source or destination race fails closed.
- Preserve the original conflict, malformed journal and candidate save slots. The copied bytes are not parsed as a valid archive, do not clear the conflict and do not start a retry. Staging or published output left by process interruption remains for manual review.
- No additional dependency or format/API migration is introduced. Tests cover Windows and POSIX process-interruption boundaries, not kernel crash/power loss, arbitrary hostile local writers or all filesystem durability semantics. Full Release CTest: Windows 44/44, WSL Ubuntu 39/39. See [06.55 report](reports/06.55-conflict-copy.md).

## D119 — Unicode path and manual dialog verification (06.56)

- Date: 2026-11-16.
- Add an integration fixture whose parent directory is `Проверка копии Ω`; verify byte-exact copy and unchanged conflicting occupant, malformed journal and candidate slot. Keep the fixture under the test-owned temporary root.
- Provide opt-in `--smoke-inspect-copy-dialog` to hold the real Editor confirmation modal open after constructing the existing isolated smoke fixture. Cancel is the required manual action for this review; it verifies destination absence and source/journal/slot preservation before the normal smoke cleanup.
- The inspection flag is test harness behavior only. It adds no public runtime feature or save format change and never opens the user's project/save directory.
- Visual inspection and exit code 0 were verified on Windows; automated integration passed on Windows and WSL Ubuntu. Do not claim visual validation on WSL, macOS, other POSIX systems or mobile targets. Full Release CTest: Windows 44/44, WSL Ubuntu 39/39. See [06.56 report](reports/06.56-unicode-copy-and-dialog-review.md).

## D120 — macOS/POSIX copy-aside portability assessment (06.57)

- Date: 2026-11-16.
- Keep the current descriptor-relative POSIX transaction unchanged in this assessment. Apple documentation confirms the relevant no-follow/directory open options, advisory `flock`, and atomic hard-link creation constraints. This makes the API path plausible for Darwin but is not evidence of a successful macOS build or runtime behavior.
- Treat `flock` as coordination among cooperating processes only. Same-directory staging and publication avoid cross-filesystem link failure by construction, but concrete volumes and providers still need testing.
- Do not describe the existing `fsync` calls as power-loss durable. Apple's `fsync(2)` documents that data can remain in device caches; `F_FULLFSYNC` is platform-specific and has filesystem/device limitations. Directory `fsync` behavior on supported macOS filesystems must be observed on actual macOS before setting the user-visible error/durability contract.
- Before advertising macOS support for this path, run focused and full Release tests on a supported macOS host/APFS: exact bytes and no-replace publication, destination collision, Unicode paths, symlink rejection, lease contention, directory/file flush outcomes, and subprocess interruption. Do not claim kernel-crash or power-loss safety based on these tests.
- 06.57 was documentation-only. Windows Release CTest passed 44/44; WSL Ubuntu Release CTest passed 39/39. Neither result is a macOS test. See [06.57 assessment](reports/06.57-posix-macos-portability-assessment.md).

## D121 — Explicit time-count conversion in cross-platform tests (06.58)

- Date: 2026-11-16.
- The user-provided macOS build result reported ambiguous `std::to_string` overloads in the C# code-workspace and build-manifest unit tests. Both formatted `std::filesystem::file_time_type::duration::rep` directly, whose underlying type can vary across standard libraries/platforms.
- Cast the test-only clock count to `long long` before calling `std::to_string`, selecting an exact standard overload. This affects only temporary test-directory naming; no production API or saved data format changes.
- Windows Release build succeeded; focused tests passed 2/2 and full CTest 44/44. WSL Ubuntu Release build succeeded; focused tests passed 2/2 and full CTest 39/39.
- These checks are not a macOS rerun. Keep macOS status unconfirmed until the user rebuilds and reruns CTest there. The reported 39 missing-executable failures were downstream consequences of the failed build, not 39 independently observed test assertion failures. See [06.58 report](reports/06.58-macos-test-portability-fix.md).

## D122 — Keep 3D prototypes modular before dependency adoption (07.1 assessment)

- Date: 2026-11-16.
- Existing implementation is 2D-only and there is no existing mesh, 3D camera, glTF importer or 3D physics contract. Preserve `astro_core` and headless builds without SDL/bgfx/import/physics dependencies.
- Reuse the existing bgfx backend only behind an optional rendering adapter for the first procedural triangle/cube prototype. Expose backend-neutral mesh/camera data; do not expose bgfx handles through Scene or the public project format.
- cgltf (MIT, reviewed upstream version 1.15) is a candidate parser for a later bounded `.gltf`/`.glb` importer, not selected or vendored. External buffer/image URI resolution must remain project-root validated and go through the asset pipeline; reject unsupported extensions rather than silently dropping required data.
- Jolt (MIT, C++17, STL-only per upstream docs) is a candidate for a later optional 3D physics adapter. It has not been built in this project or in CI and is not approved as an unconditional dependency. Box2D remains 2D-only for this decision.
- No versions are pinned and no dependency was downloaded. First coding slice: camera/projection and backend-neutral mesh contracts plus a visual bgfx smoke and baseline metrics. Then importer and physics are separate slices with their own security/performance tests. Do not alter Scene serialization or claim 3D support before those tests and migration conventions exist.
- Upstream sources reviewed 2026-11-16: [bgfx repository and BSD-2-Clause](https://github.com/bkaradzic/bgfx), [cgltf parser](https://github.com/jkuhlmann/cgltf/blob/master/cgltf.h) and [MIT license](https://github.com/jkuhlmann/cgltf/blob/master/LICENSE), [Jolt repository/build/platform notes](https://github.com/jrouwe/JoltPhysics) and [MIT license](https://github.com/jrouwe/JoltPhysics/blob/master/LICENSE), [Khronos glTF 2.0.1 spec](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html).
- See [07.1 assessment](reports/07.1-3d-foundation-assessment.md).
- Parser candidate status is superseded by the security/dependency review in [D124](#d124--bounded-gltf-parser-candidate-and-dependency-gate-071b): the local cgltf 1.15 copy is not approved for importer use; fastgltf v1.0.1 is the recommended candidate for a separately gated prototype.

## D123 — 3D camera/mesh prototype boundary (07.1a)

- Date: 2026-11-16.
- Keep the first 3D contract in `astro_runtime_render3d`, without a dependency from `astro_core`, Scene serialization or Editor. The data API uses right-handed camera coordinates (+Y up, forward -Z), column-major matrices, selectable [0,1]/[-1,1] projection depth, and validated indexed triangle lists.
- The optional renderer smoke is isolated under the existing Windows SDL3/bgfx build and initially targets D3D11. Reuse cached upstream bgfx shader binaries at build time; copy them only into the ignored build output, do not vendor source or generated shaders into the repository.
- This smoke validates coarse pixel presence and composition bounds through bgfx readback, but it is not a golden-image comparison. Do not claim production scene rendering or cross-platform 3D until exact image expectations and another separately built backend are validated.
- Keep glTF/GLB import, normals/materials, Scene component/versioning, Editor viewport integration and 3D physics as later independent slices with their own security, migration and regression coverage. No new external package was installed.
- Windows Release build: 161/161 actions; CTest 46/46. WSL Ubuntu Release build and CTest 40/40. Details: [07.1a report](reports/07.1a-3d-camera-mesh-prototype.md).

## D124 — Bounded glTF parser candidate and dependency gate (07.1b)

- Date: 2026-11-16.
- Do not adopt the `cgltf.h` found under bgfx's third-party directory for project asset import. The reviewed upstream cgltf 1.15 source has public unresolved memory-safety reports, including integer-overflow/OOB-read claims in validation; input files must be treated as untrusted. This is a scoped dependency risk finding, not a claim that all cgltf releases or every code path are affected.
- Evaluate released `fastgltf` v1.0.1 as the next prototype candidate: C++20, MIT. Its pinned CMake revision defaults `FASTGLTF_DOWNLOAD_SIMDJSON` and `FASTGLTF_DOWNLOAD_SIMDUTF` to ON and selects simdjson 5.1.0 and simdutf 9.3.3. A build must prevent unexpected downloads and either provide verified local sources or use explicit dependency acquisition in an ignored build cache; examples/tests and their glm/Catch2 fetches stay disabled unless separately needed. The reviewed licenses are MIT for fastgltf and the two embedded parsing/encoding dependencies.
- Candidate selection is not production approval. The importer must bound source, buffers, counts and decoded geometry; reject URI traversal, absolute/out-of-project paths and unsupported required extensions; confine all external resources to the canonical project root; preserve source bytes; publish derived cache atomically; and cover malformed/untrusted inputs, missing buffers, cache corruption and repeat import. Run fuzz/sanitizer coverage when toolchain support exists.
- No dependency download, vendoring, importer implementation, Scene format change or public API change occurred in this assessment. Permission for a local ignored build-cache acquisition is required before the next implementation slice under the project rule for installations/downloads.
- Windows Release verification remained 161/161 build actions and CTest 46/46 after the 07.1a readback extension. See [07.1b assessment](reports/07.1b-gltf-importer-dependency-assessment.md).

## D125 — Optional bounded glTF static mesh importer (07.1b)

- Date: 2026-11-17.
- Implement the first importer slice as a separate opt-in `astro_gltf` target; default headless configurations must neither locate nor fetch its third-party dependencies. Use fastgltf v1.0.1 at commit `87cfbceeeda127ccc9092694279437b700046119`, with simdjson 5.1.0 and simdutf 9.3.3 staged only in the ignored local build cache. CMake verifies the pinned commit and four source-file hashes, and explicitly disables fastgltf's dependency downloads and examples/tests/docs/install.
- The first supported product is backend-neutral static triangle geometry: float VEC3 POSITION plus optional unsigned indices. Import is explicit and returns `MeshData`; do not change Scene/AssetRegistry formats or claim general model support in this slice.
- Accept source files only under `Assets/`; constrain plain external buffer URIs to regular files below that directory; enforce explicit byte/count limits; reject unsupported required extensions and unsupported vertex/morph/compression data rather than silently losing it. Preserve source files. Use a versioned, checksummed derived cache under `Library/Derived/gltf` and repair a corrupt generated cache on reimport.
- Windows x64 Release opt-in build passed 95/95 Ninja actions and full CTest passed 42/42. Default headless Release configure/build passed with the option OFF and without an `astro_gltf` target. Focused adversarial integration test passed 1/1. These checks do not establish fuzz/sanitizer safety or other-platform support. Filesystem path inspection is not safe against hostile concurrent replacement races.
- The old no-download assessment is historical and remains useful for its candidate-selection rationale; implementation details and remaining limits are in the [07.1b report](reports/07.1b-bounded-gltf-importer.md).

## D126 — Scene model asset references (07.1c)

- Date: 2026-11-17.
- Add a minimal `MeshRenderer` Scene component that references a stable model AssetId and selects a mesh/primitive. Keep imported geometry backend-neutral; do not add renderer handles or third-party types to Scene or serialized project data.
- Advance Scene serialization to v6 and Asset index to v3 while retaining readers for the prior supported versions. Component changes use the typed command API and preserve component identity for undo/redo.
- Register models only through an explicit successful `GltfImporter::importAndRegister` call. Persist the top-level source size/hash and importer content digest. Reimport is the freshness check for external buffer bytes; no independent per-buffer dependency record is introduced in this slice.
- Prefab capture rejects MeshRenderer until prefab format/override semantics are implemented, preventing silent data loss. Scene/Game View rendering is also deferred.
- Windows x64 Release clean build passed 158/158 actions; focused integration passed 5/5; final full CTest passed 42/42 in 7.94 s. Fresh default headless Release build with importer/testing disabled passed 32/32 actions. macOS/mobile, fuzz/sanitizer and visual renderer validation remain unverified. See [07.1c report](reports/07.1c-scene-model-asset-reference.md).

## D127 — Scene MeshRenderer resolution (07.1d)

- Date: 2026-11-17.
- Resolve Scene geometry in the optional 3D/import adapter rather than adding importer or renderer-library dependencies to Scene or `astro_core`. The resolver accepts a `MeshRendererComponent`, returns backend-neutral selected `MeshData`, mesh name and visibility, and performs no drawing.
- Check component schema and nonzero registered AssetId; validate the model source with AssetRegistry; import current inputs and compare the persisted importer digest so external-buffer changes cannot silently render stale registered content; then validate mesh and primitive selectors. Return clear errors on stale/missing/invalid references. Reimport/register explicitly to update the content digest while retaining the AssetId.
- Keep the resolver stateless and return owned geometry; moving the selected primitive avoids copying its vertex/index arrays. No per-buffer graph or long-lived decoded geometry cache is added.
- Windows x64 Release clean build passed 159/159 actions; focused integration passed 1/1 and final full CTest passed 42/42 in 2.51 s. The headless configuration reconfigured/built with importer/testing disabled and contained no 3D/import targets. macOS/mobile, fuzz/sanitizer and visual rendering remain unverified. See [07.1d report](reports/07.1d-scene-mesh-resolution.md).

## D128 — Standalone Scene Mesh BGFX preview (07.1e)

- Date: 2026-11-17.
- First consume Scene model references in a standalone opt-in Windows D3D11/BGFX preview/smoke instead of extending the large Editor viewport path in this substage. The sample imports/registers a model, loads a versioned Scene fixture, resolves its MeshRenderer and submits its selected primitive.
- Keep the existing procedural cube smoke unchanged and available with glTF importer OFF. Register Scene preview CTest only when both the BGFX sample and glTF importer are enabled. CTest copies fixture files into a unique build-local temporary project and removes only that test-owned directory.
- Bound the sample uploader to 65,536 vertices and 196,608 indices for 16-bit index buffers. It previews one visible primitive at the origin, uses no Scene transform, disables back-face culling for the selected primitive and checks coarse framebuffer coverage/bounds rather than a golden image.
- Windows x64 Release focused graphics smoke passed 2/2; new Scene mode read back 24,642 foreground pixels with bounds (369,210)–(590,430). Final post-regeneration CTest passed 45/45 in 6.10 s (an earlier full run also passed in 12.96 s). Importer-off procedural smoke passed 1/1; headless configuration remained free of 3D targets. Editor/Game View/runtime integration and non-Windows backends remain unverified. See [07.1e report](reports/07.1e-scene-bgfx-preview.md).

## D129 — Editor Scene View Mesh preview (07.1f)

- Date: 2026-11-17.
- Connect the existing optional importer/resolver to Editor Scene View only. Keep `astro_gltf` and shader binaries conditional, so importer-off Editor builds retain the existing 2D path. Do not connect Game View or gameplay runtime in this slice.
- Reuse the Scene View BGFX color target with a depth attachment and a dedicated ordered 3D pass. Keep per-entity static vertex/index buffers across frames and destroy them on reference/entity removal or viewport teardown; recreate framebuffer attachments on resize. Provide explicit cache invalidation because the current asset registry has no filesystem watcher or importer digest notification to the Editor.
- Compose local transforms through parent hierarchy, rejecting missing transforms, non-finite values, zero scale, missing parents and cycles. The Editor smoke verifies a child origin after a parent scale and local offset, in addition to one submitted mesh draw.
- This remains a Windows D3D11 prototype: fixed camera, position/color-only triangles, 65,536 vertex / 196,608 index upload cap, and no materials/normals/lighting/picking. Do not describe this as Game View or runtime 3D support.
- Windows x64 Release importer-on build succeeded; focused Editor/BGFX smoke passed 3/3; full CTest passed 49/49. Importer-off Editor build succeeded and focused Editor smoke/localization passed 2/2. The new UI labels are covered by localization tests. No macOS/mobile backend or golden-image comparison was run. See [07.1f report](reports/07.1f-editor-scene-mesh-preview.md).

## D130 — Scene View camera and Frame All (07.1g)

- Date: 2026-11-17.
- Keep camera state and orbit/pan/zoom/frame mathematics in an Editor-only backend-neutral module; no renderer or UI dependency is required to unit-test it. Consume mouse controls only when the rendered Scene View viewport is hovered.
- Frame visible Scene meshes using the world-space AABB formed from all eight local-bounds corners after the complete hierarchy world transform. Reject a mesh's bounds as a whole when transformed coordinates are non-finite or outside the Scene View range; do not merge partial corner results.
- Preserve current position/color-only mesh pass and limit this feature to Scene View. Game View/runtime 3D, materials, lighting, picking, and cross-platform backend support are separate work.
- Windows x64 Release focused tests passed 5/5 in 5.02 s and complete CTest passed 50/50 in 9.82 s. Direct Editor smoke reported 139 3D submissions, child world origin `(2.75,0,0)`, and `frameAll=1`. This validates mathematical fit and submission/lifecycle, not visual pixel equivalence. See [07.1g report](reports/07.1g-editor-scene-camera.md).

## D131 — Scene View visual smoke and mesh diagnostics (07.1h)

- Date: 2026-11-17.
- Extend Editor smoke only with a test-owned deterministic scene containing two valid mesh references and one unresolved AssetId. Require valid submissions to continue despite the unresolved component and verify the diagnostic explicitly.
- For visual evidence, capture the existing BGFX window swapchain and crop the Scene View image using its normalized viewport rectangle. Require a small near-white pixel coverage floor for the white glTF fixture. Do not commit a pixel golden or treat the observed count as portable: BGFX screenshot capture in this integration accepts a window framebuffer, and rasterization varies by driver.
- Preserve existing user/project resources; the test fixture is copied/generated only under the isolated smoke directory and removed after testing.
- Windows x64 Release focused CTest passed 5/5 in 4.85 s; full CTest passed 50/50 in 9.47 s. The direct smoke observed two submissions in the last frame, one missing-AssetId diagnostic, Frame All center x=4, and 11,900 near-white viewport pixels. See [07.1h report](reports/07.1h-editor-scene-visual-smoke.md).

### D132 — Editor 3D frame profile scope (07.1i)

Keep 07.1i as an opt-in measurement mode (`astro_editor.exe --benchmark-3d`) using fixed 1/100/500/1000-mesh Scene View projects, explicit warm-up and measured frame counts, and separate CPU mesh preparation/submission/full-editor frame metrics. Report the BGFX GPU timer only as a full-window-frame timer because it spans the frame and is affected by VSync; do not interpret it as isolated Scene View GPU cost. Record process memory, fixture import/setup, and executable size as context. No performance thresholds are selected here; establish target hardware, visual quality, and budgets before adding pass/fail performance limits. The profile is an Editor smoke/diagnostic, not an optimizer or cross-platform benchmark. See [07.1i report](reports/07.1i-editor-3d-frame-profile.md).

#include "Localization.hpp"

#include <array>
#include <fstream>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace astro::editor
{
namespace
{
struct Entry final { const char* english; const char* russian; };

constexpr Entry entries[]{
    {"Activation", "Активация"}, {"Add", "Добавить"}, {"Add authored level", "Добавить существующий уровень"},
    {"Add key at end", "Добавить ключ в конец"}, {"Add level", "Добавить уровень"}, {"Add score rule", "Добавить правило по очкам"},
    {"Anchor max X", "Максимум якоря X"}, {"Anchor max Y", "Максимум якоря Y"},
    {"Anchor min X", "Минимум якоря X"}, {"Anchor min Y", "Минимум якоря Y"},
    {"Anchors are normalized to the parent area; offsets are pixels.", "Якоря заданы относительно области родителя, смещения — в пикселях."},
    {"And", "И"}, {"Apply project settings", "Применить настройки проекта"},
    {"Area half-height", "Половина высоты области"}, {"Area half-width", "Половина ширины области"},
    {"Assets-relative image path", "Путь к изображению относительно Assets"}, {"Bind property", "Привязать свойство"},
    {"Camera Presets", "Пресеты камеры###CameraPresets"}, {"Campaign authoring", "Редактор кампании"},
    {"Campaign ended. The final board remains visible until restart or Stop.", "Кампания завершена. Последний уровень останется на экране до перезапуска или остановки."},
    {"Campaign ID", "ID кампании"}, {"Campaign name", "Название кампании"},
    {"Campaign progress is stored in user data and changes only when you save or load a slot.", "Прогресс кампании хранится в данных пользователя и меняется только при сохранении или загрузке слота."},
    {"Campaign progress saves are unavailable in this Editor build.", "Сохранения прогресса кампании недоступны в этой сборке редактора."},
    {"Campaign save slot", "Слот сохранения кампании"}, {"Campaign save folder: %s", "Папка сохранения кампании: %s"},
    {"Cancel", "Отмена"}, {"Cancel rename", "Отменить переименование"},
    {"Change preview size to check anchors and offsets.", "Измените размер предпросмотра, чтобы проверить якоря и смещения."},
    {"Changing profile version or folder does not delete previous slot files.", "Изменение версии профиля или папки не удаляет прежние файлы слотов."},
    {"Checkpoint XY map", "Карта XY контрольных точек"}, {"Checkpoints", "Контрольные точки"},
    {"Clear binding", "Очистить привязку"}, {"Click a cell to cycle its tile ID. Grid edits affect the draft only.", "Нажимайте на клетку, чтобы менять её тип. Изменения сетки затрагивают только черновик."},
    {"Click two neighboring tiles to swap. Moves resolve on the fixed runtime step.", "Нажмите две соседние плитки, чтобы поменять их местами. Ход выполняется на фиксированном шаге runtime."},
    {"Close project", "Закрыть проект"}, {"Computed pose", "Вычисленная поза"}, {"Concatenate", "Объединить"},
    {"Confirm Puzzle level ID rename", "Подтвердить переименование ID уровня Puzzle"}, {"Content", "Содержимое"},
    {"Continue campaign", "Продолжить кампанию"}, {"create a group first", "сначала создайте группу"},
    {"C# scripts", "Скрипты C#"}, {"C# build: current", "Сборка C#: актуальна"},
    {"Metadata inspection is explicit, does not run scripts, and requires a current registered build.",
        "Проверка метаданных запускается вручную, не исполняет скрипты и требует актуальную зарегистрированную сборку."},
    {"CLR metadata validation", "Проверка метаданных CLR"},
    {"The selected assembly is copied into a temporary AppContainer worker; no user script code is executed.",
        "Сборка копируется во временный изолированный AppContainer; пользовательский код скриптов не запускается."},
    {"Validating C# metadata...", "Проверка метаданных C#..."},
    {"Inspect registered C# types", "Проверить зарегистрированные типы C#"},
    {"C# metadata validation could not start", "Не удалось запустить проверку метаданных C#"},
    {"C# metadata validation started", "Проверка метаданных C# запущена"},
    {"C# metadata validation passed", "Проверка метаданных C# завершена успешно"},
    {"C# metadata validation failed", "Проверка метаданных C# завершилась с ошибкой"},
    {"Metadata validation result", "Результат проверки метаданных"},
    {"Requested %zu registered types in %zu isolated batches; report cap is 128 KiB.",
        "Запрошено типов: %zu; изолированных пакетов: %zu; предел отчёта — 128 КиБ."},
    {"C# build: stale", "Сборка C#: устарела"}, {"C# build: invalid", "Сборка C#: некорректна"},
    {"No successful C# build found", "Успешная сборка C# не найдена"},
    {"C# ScriptBehavior", "Скриптовые компоненты C#"},
    {"C# Code", "Код C#"}, {"Project scripts are confined to Scripts/*.cs and saved as UTF-8.",
        "Скрипты проекта находятся только в Scripts/*.cs и сохраняются в UTF-8."},
    {"Open a project to edit C# scripts.", "Откройте проект для редактирования скриптов C#."},
    {"C# build settings", "Настройки сборки C#"},
    {"Managed type existence is checked only when an approved Play session loads the script.",
        "Наличие managed type проверяется только при загрузке скрипта в подтверждённой Play-сессии."},
    {"Registered managed type differs from the current build binding; register the current build",
        "Зарегистрированный managed type не совпадает с привязкой текущей сборки; зарегистрируйте сборку заново"},
    {"Registered source hash differs from the current build; register the current build",
        "Хэш зарегистрированного исходника не совпадает с текущей сборкой; зарегистрируйте сборку заново"},
    {"Registered assembly output differs from the current build; register the current build",
        "Выходные файлы зарегистрированной сборки отличаются; зарегистрируйте текущую сборку заново"},
    {"No C# build manifest yet. Save settings to create one.", "Манифест сборки C# ещё не создан. Сохраните настройки, чтобы создать его."},
    {"Replace invalid manifest...", "Заменить повреждённый манифест..."},
    {"Pinned .NET SDK", "Закреплённая версия .NET SDK"},
    {"Configured local SDK: %s", "Настроенная локальная SDK: %s"}, {"not detected", "не обнаружена"},
    {"Target framework", "Целевая платформа"}, {"Target framework: %s", "Целевая платформа: %s"},
    {"C# assembly name", "Имя сборки C#"},
    {"Script type bindings", "Привязки типов скриптов"}, {"Managed type name", "Имя управляемого типа"},
    {"Remove binding", "Удалить привязку"}, {"Binding source", "Исходный файл привязки"},
    {"Select a C# source", "Выберите исходный файл C#"}, {"New managed type name", "Имя нового управляемого типа"},
    {"Add binding", "Добавить привязку"}, {"Refresh C# source list", "Обновить список файлов C#"},
    {"Save C# build settings", "Сохранить настройки сборки C#"},
    {"Replace the invalid C# build manifest with fresh defaults?", "Заменить повреждённый манифест C# новыми настройками по умолчанию?"},
    {"Replace with defaults", "Заменить настройками по умолчанию"}, {"C# build settings saved", "Настройки сборки C# сохранены"},
    {"Open script path under Scripts", "Путь к скрипту относительно Scripts"}, {"Open script", "Открыть скрипт"},
    {"New script path under Scripts", "Путь нового скрипта относительно Scripts"}, {"Create script", "Создать скрипт"},
    {"Scripts", "Скрипты"}, {"Unsaved", "Есть несохранённые изменения"}, {"Save script", "Сохранить скрипт"},
    {"Build C# project", "Собрать проект C#"}, {"Building C#...", "Сборка C#..."},
    {"C# build unavailable in this configuration", "Сборка C# недоступна в этой конфигурации"},
    {"C# project build started", "Сборка проекта C# началась"}, {"C# build succeeded", "Сборка C# завершена успешно"},
    {"C# build failed", "Сборка C# завершилась с ошибкой"}, {"Compiler diagnostics", "Диагностика компилятора"},
    {"C# editor source size limit is 4 MiB", "Размер исходного файла C# ограничен 4 МиБ"},
    {"Build output", "Вывод сборки"},
    {"Save changes before switching C# scripts?", "Сохранить изменения перед переключением скрипта C#?"},
    {"Wait for the C# build to finish before switching projects", "Дождитесь завершения сборки C#, прежде чем переключать проекты"},
    {"Save and continue", "Сохранить и продолжить"}, {"Discard and continue", "Отменить изменения и продолжить"},
    {"Script opened", "Скрипт открыт"}, {"Script created", "Скрипт создан"}, {"Script saved", "Скрипт сохранён"},
    {"Add disabled script", "Добавить выключенный скрипт"},
    {"No current registered C# scripts. Refresh and register a current build in Project.",
        "Нет зарегистрированных актуальных скриптов C#. Обновите состояние и зарегистрируйте сборку в панели проекта."},
    {"Select a registered script", "Выберите зарегистрированный скрипт"},
    {"Registered script", "Зарегистрированный скрипт"},
    {"This entity has no ScriptBehavior components.", "У этой сущности нет компонентов ScriptBehavior."},
    {"Component %llu — AssetId %llu — %s", "Компонент %llu — AssetId %llu — %s"},
    {"Script binding is missing or does not match the registry.", "Привязка скрипта отсутствует или не соответствует реестру."},
    {"C# build is not current; this binding cannot run.", "Сборка C# неактуальна; этот скрипт нельзя запускать."},
    {"Remove script", "Удалить скрипт"},
    {"C# ScriptBehavior added disabled", "Компонент ScriptBehavior добавлен и выключен"},
    {"C# ScriptBehavior enabled", "Компонент ScriptBehavior включён"},
    {"C# ScriptBehavior disabled", "Компонент ScriptBehavior выключен"},
    {"C# ScriptBehavior removed", "Компонент ScriptBehavior удалён"},
    {"Refresh C# status", "Обновить состояние C#"}, {"Register current C# build", "Зарегистрировать текущую сборку C#"},
    {"registered AssetId", "зарегистрирован AssetId"}, {"not registered", "не зарегистрирован"},
    {"Create a scene entity to bind values.", "Создайте сущность сцены, чтобы привязать значения."},
    {"Create a scene entity to use a follow preset.", "Создайте сущность сцены для пресета слежения."},
    {"Create empty profile", "Создать пустой профиль"}, {"Create group", "Создать группу"},
    {"Create or open a project first.", "Сначала создайте или откройте проект."}, {"Create or select an element.", "Создайте или выберите элемент."},
    {"Custom pose", "Пользовательская поза"}, {"Delete subtree", "Удалить поддерево"}, {"Diagnostics", "Диагностика###Diagnostics"},
    {"Discard and continue", "Отбросить изменения и продолжить"}, {"Discard current campaign draft changes?", "Отбросить изменения черновика кампании?"},
    {"Discard Puzzle campaign draft?", "Отбросить черновик кампании Puzzle?"}, {"Discard unsaved Puzzle level changes?", "Отбросить несохранённые изменения уровня Puzzle?"},
    {"Divide", "Разделить"}, {"Down", "Вниз"}, {"Draft has unsaved changes", "В черновике есть несохранённые изменения"},
    {"Duplicate name", "Имя копии"}, {"Duplicate selected", "Дублировать выбранный"}, {"Edit", "Правка"},
    {"Element removed.", "Элемент удалён."}, {"Enabled", "Включено"}, {"Entity", "Сущность"}, {"Equal number", "Равно числу"},
    {"Exit", "Выход"}, {"Explicitly selected fields only. Authoring currently stays in editor memory.", "Сохраняются только явно выбранные поля. Настройки авторинга пока находятся в памяти редактора."},
    {"File", "Файл"}, {"Follow entity", "Отслеживать сущность"}, {"Game View", "Игровой вид###GameView"},
    {"Game View is too small to display the Puzzle board.", "Окно игрового вида слишком мало для отображения доски Puzzle."},
    {"Greater than", "Больше чем"}, {"Group", "Группа"}, {"Groups", "Группы"},
    {"Groups are explicit entity lists; no tag inference is performed.", "Группы задаются явными списками сущностей; теги автоматически не анализируются."},
    {"Height", "Высота"}, {"Hierarchy", "Иерархия###Hierarchy"}, {"Hierarchy map", "Карта иерархии"},
    {"Image preview unavailable (decode/upload failed)", "Предпросмотр изображения недоступен (ошибка декодирования или загрузки)"},
    {"Import a PNG or JPEG in Project first.", "Сначала импортируйте PNG или JPEG в проект."}, {"Import PNG/JPEG", "Импортировать PNG/JPEG"},
    {"Imported images", "Импортированные изображения"}, {"Inspector", "Инспектор###Inspector"}, {"Keep editing", "Продолжить редактирование"},
    {"Keyframes", "Ключевые кадры"}, {"Less than", "Меньше чем"}, {"Level %s — OK", "Уровень %s — доступен"},
    {"Level goal reached. Continue when ready; the authored transition rule will be applied.", "Цель уровня достигнута. Нажмите «Продолжить», когда будете готовы; будет применено заданное правило перехода."},
    {"Level ID", "ID уровня"}, {"Level name", "Название уровня"}, {"Load level", "Загрузить уровень"}, {"Load progress", "Загрузить прогресс"},
    {"Level %s — MISSING/INVALID: %s", "Уровень %s — ОТСУТСТВУЕТ/НЕДОПУСТИМ: %s"},
    {"Load selected campaign", "Загрузить выбранную кампанию"}, {"Members: %zu", "Участников: %zu"},
    {"%zu levels — link issues", "Уровней: %zu — проблемы ссылок"},
    {"Move queued for the next runtime step.", "Ход поставлен в очередь на следующий шаг runtime."}, {"Multiply", "Умножить"},
    {"New campaign", "Новая кампания"}, {"New group ID", "ID новой группы"}, {"New level", "Новый уровень"},
    {"New level ID", "ID нового уровня"}, {"New profile ID", "ID нового профиля"}, {"New project...", "Новый проект..."},
    {"New scene", "Новая сцена"}, {"No campaign files found.", "Файлы кампаний не найдены."}, {"No errors recorded", "Ошибок нет"},
    {"No saved levels yet.", "Сохранённых уровней пока нет."}, {"Not", "НЕ"}, {"Offset max X", "Максимальное смещение X"},
    {"Offset max Y", "Максимальное смещение Y"}, {"Offset min X", "Минимальное смещение X"}, {"Offset min Y", "Минимальное смещение Y"},
    {"Open a project to browse its campaign files.", "Откройте проект, чтобы просмотреть файлы кампаний."},
    {"Open a project to configure Puzzle saves.", "Откройте проект, чтобы настроить сохранения Puzzle."},
    {"Open a project to launch an authored campaign.", "Откройте проект, чтобы запустить созданную кампанию."},
    {"Open another level or start a new draft and discard the current unsaved edits?", "Открыть другой уровень или новый черновик и отбросить несохранённые изменения?"},
    {"Open project...", "Открыть проект..."}, {"Open scene", "Открыть сцену"}, {"Open scene...", "Открыть сцену..."},
    {"Open selected", "Открыть выбранный"}, {"Or", "ИЛИ"}, {"Pause", "Пауза"}, {"Play", "Играть"},
    {"Play authored level", "Запустить созданный уровень"}, {"Play Campaign", "Запустить кампанию"}, {"Play Puzzle", "Запустить Puzzle"},
    {"Playhead", "Курсор времени"}, {"Pose preview only; camera image rendering is not connected.", "Только предпросмотр позы; отображение изображения с камеры не подключено."},
    {"Position X", "Позиция X"}, {"Position Y", "Позиция Y"}, {"Position Z", "Позиция Z"},
    {"Preset", "Пресет"}, {"Preview size", "Размер предпросмотра"}, {"Profile", "Профиль"},
    {"Profiles start empty; add fields explicitly.", "Профили изначально пусты; добавляйте поля вручную."}, {"Project", "Проект###Project"},
    {"Project content: Puzzle/Campaigns/*.astro-puzzle-campaign", "Файлы проекта: Puzzle/Campaigns/*.astro-puzzle-campaign"},
    {"Project level catalog", "Каталог уровней проекта"}, {"Project name", "Имя проекта"}, {"Property", "Свойство"},
    {"Puzzle Campaign Browser", "Кампании Puzzle###PuzzleCampaignBrowser"}, {"Puzzle Level Authoring", "Авторинг уровней Puzzle"},
    {"Puzzle Save Settings", "Настройки сохранений Puzzle"}, {"Puzzle slot", "Слот Puzzle"}, {"Recent diagnostics", "Последние сообщения диагностики"},
    {"Preview save transfer", "Предпросмотр переноса сохранений"},
    {"Copy campaign saves…", "Копировать сохранения кампании…"},
    {"Review these paths before copying campaign saves.", "Проверьте эти пути перед копированием сохранений кампании."},
    {"Copy summary: %llu save slots, %llu bytes. Existing files will not be overwritten.",
        "Будут скопированы слоты: %llu, байт: %llu. Существующие файлы не перезаписываются."},
    {"Copying will create files in the target folder. The source remains unchanged.",
        "Копирование создаст файлы в целевой папке. Источник останется без изменений."},
    {"Copy reviewed save files", "Копировать проверенные файлы сохранений"},
    {"Cancel copy", "Отменить копирование"},
    {"Campaign saves copied", "Сохранения кампании скопированы"},
    {"Copy confirmation required", "Для копирования требуется подтверждение"},
    {"Source changed; preview again", "Источник изменился; выполните предпросмотр заново"},
    {"Target changed; preview again", "Целевая папка изменилась; выполните предпросмотр заново"},
    {"Target conflict; no files overwritten", "Конфликт в целевой папке; файлы не перезаписаны"},
    {"Source is not transferable", "Источник нельзя перенести"},
    {"Copy cancelled; created files rolled back", "Копирование отменено; созданные файлы удалены"},
    {"Copy failed; rollback completed", "Ошибка копирования; созданные файлы удалены"},
    {"Rollback incomplete; inspect remaining files", "Откат выполнен не полностью; проверьте оставшиеся файлы"},
    {"Remaining file: %s", "Оставшийся файл: %s"},
    {"%s — %llu/%llu files, %llu bytes", "%s — файлов: %llu из %llu, байт: %llu"},
    {"An interrupted campaign copy was detected. Review its files before another copy.",
        "Обнаружена прерванная копия кампании. Проверьте её файлы перед новым копированием."},
    {"Recovery journal: %s", "Журнал восстановления: %s"},
    {"Journal campaign ID: %s", "ID кампании в журнале: %s"},
    {"Recovery detail: %s", "Подробности восстановления: %s"},
    {"Recovery artifact — slot %d, %s, %llu/%llu bytes: %s",
        "Артефакт восстановления — слот %d, %s, байт %llu из %llu: %s"},
    {"Missing", "Отсутствует"}, {"Complete", "Полный"}, {"Partial", "Частичный"},
    {"Changed", "Изменён"}, {"Unsafe path", "Небезопасный путь"},
    {"Review and dismiss recovery record…", "Проверить и закрыть запись восстановления…"},
    {"This removes only the recovery journal. It does not remove, repair, or overwrite any save file.",
        "Будет удалён только журнал восстановления. Файлы сохранений не будут удалены, исправлены или перезаписаны."},
    {"Inspect or clean partial save files manually before starting another copy.",
        "Перед новым копированием вручную проверьте или очистите частичные файлы сохранений."},
    {"Journal: %s", "Журнал: %s"},
    {"Acknowledge recovery record", "Подтвердить просмотр записи"},
    {"Cancel recovery review", "Отменить просмотр восстановления"},
    {"Recovery result: %s", "Результат восстановления: %s"},
    {"Resume interrupted transfer…", "Продолжить прерванный перенос…"},
    {"Resume rechecks every source file against the journal. Complete target files are kept; only missing files are created exclusively.",
        "При продолжении каждый исходный файл сверяется с журналом. Полные файлы назначения сохраняются; новые эксклюзивно создаются только для отсутствующих."},
    {"Any changed source or partial, changed, or unsafe target blocks the operation. No existing save is repaired or overwritten.",
        "Изменённый источник, частичная запись, изменённый или небезопасный файл назначения остановят операцию. Существующие сохранения не исправляются и не перезаписываются."},
    {"Partial target files are left untouched. Review their contents manually; resume will not delete, repair, append to, or overwrite them.",
        "Частичные файлы назначения остаются без изменений. Проверьте их содержимое вручную: продолжение не удаляет, не исправляет, не дописывает и не перезаписывает их."},
    {"Unknown journal problem", "Неизвестная проблема журнала"},
    {"Journal is missing", "Журнал отсутствует"}, {"Journal is valid", "Журнал корректен"},
    {"Journal could not be read", "Не удалось прочитать журнал"},
    {"Journal path is unsafe or not a regular file", "Путь к журналу небезопасен или это не обычный файл"},
    {"Journal exceeds the supported size limit", "Размер журнала превышает допустимый предел"},
    {"Journal is malformed, truncated, or uses an unsupported version",
        "Журнал повреждён, усечён или использует неподдерживаемую версию"},
    {"Recovery journal status: %s", "Состояние журнала восстановления: %s"},
    {"No save artifact paths were inferred and no save files were changed. Copy and resume are unavailable. A readable malformed journal can be copied to an archive after confirmation; the original remains and continues to block transfers. Other cases require manual handling.",
        "Пути файлов сохранений не определялись, файлы не изменялись. Копирование и продолжение недоступны. Читаемый повреждённый журнал можно скопировать в архив после подтверждения; оригинал остаётся и продолжает блокировать переносы. Остальные случаи требуют ручной проверки."},
    {"Create archive copy of malformed journal…", "Создать архивную копию повреждённого журнала…"},
    {"This copies the exact journal bytes to a new adjacent archive file and verifies that copy. The original journal remains in place and continues to block transfers until manually reviewed.",
        "Точные байты журнала копируются в новый соседний архивный файл, после чего копия проверяется. Исходный журнал остаётся на месте и блокирует переносы до ручной проверки."},
    {"No campaign slot/save files are inspected, inferred, changed or removed. A conflicting archive file is never replaced.",
        "Файлы слотов/сохранений кампании не анализируются, не определяются, не меняются и не удаляются. Существующий архивный файл не заменяется."},
    {"Archive folder: %s", "Папка архива: %s"},
    {"Create archive copy", "Создать архивную копию"},
    {"Cancel journal archive", "Отменить архивирование журнала"},
    {"Malformed journal archive copy created; original journal remains.", "Архивная копия повреждённого журнала создана; исходный журнал сохранён."},
    {"Archive path: %s", "Путь архива: %s"},
    {"Archive object: %s", "Объект архива: %s"},
    {"Archive comparison: %s", "Сравнение архива: %s"},
    {"Observed bytes: %llu", "Проверено байт: %llu"},
    {"Archive path is missing", "Файл архива отсутствует"},
    {"Archive path is a regular file", "По пути находится обычный файл"},
    {"Archive path is a directory", "По пути находится каталог"},
    {"Archive path is a link or reparse point", "По пути находится ссылка или точка повторной обработки"},
    {"Archive path is another file type", "По пути находится объект другого типа"},
    {"Archive path is unavailable", "Путь архива недоступен"},
    {"Content was not read", "Содержимое не читалось"},
    {"Bytes exactly match the reviewed journal", "Байты полностью совпадают с проверенным журналом"},
    {"Bytes differ from the reviewed journal", "Байты отличаются от проверенного журнала"},
    {"Bytes are a prefix of the reviewed journal", "Байты являются началом проверенного журнала"},
    {"File exceeds the journal size limit", "Размер файла превышает предел журнала"},
    {"Content could not be compared safely", "Не удалось безопасно сравнить содержимое"},
    {"A prefix match does not identify who created the file. No files were changed by this inspection.",
        "Совпадение начала файла не показывает, кто его создал. При проверке файлы не изменялись."},
    {"Existing archive conflict inspected; no files were changed.",
        "Конфликт существующего архива проверен; файлы не изменялись."},
    {"Prepare separate unverified copy…", "Подготовить отдельную непроверенную копию…"},
    {"A separate byte-for-byte copy will be created. The original remains untouched.",
        "Будет создана отдельная точная копия. Исходный файл останется нетронутым."},
    {"Copy source: %s", "Источник копии: %s"},
    {"Copy destination: %s", "Путь копии: %s"},
    {"Copy size: %llu bytes", "Размер копии: %llu байт"},
    {"The file creator is unknown. This copy is unverified and will not clear the original conflict.",
        "Автор файла неизвестен. Копия не проверена и не устранит исходный конфликт."},
    {"Create separate copy", "Создать отдельную копию"},
    {"Cancel separate copy", "Отменить создание копии"},
    {"Unverified copy created; the original conflict remains.",
        "Непроверенная копия создана; исходный конфликт сохраняется."},
    {"Copy-aside result: %s", "Результат сохранения копии: %s"},
    {"No reviewed archive conflict is available for copying.",
        "Нет проверенного конфликта архива для копирования."},
    {"Archive conflict copy smoke did not preserve its source files.",
        "Проверка копии архива изменила исходные файлы."},
    {"Copy preview is no longer available.", "Предпросмотр копии больше недоступен."},
    {"Close copy preview", "Закрыть предпросмотр копии"},
    {"Resume reviewed transfer", "Продолжить проверенный перенос"}, {"Cancel resume", "Отменить продолжение"},
    {"Interrupted campaign transfer resumed", "Прерванный перенос кампании продолжен"},
    {"%s — %llu copied, %llu skipped, %llu attempted, %llu bytes",
        "%s — скопировано %llu, пропущено %llu, попыток %llu, байт %llu"},
    {"Recovery record acknowledged. Save files were not changed.",
        "Запись восстановления закрыта. Файлы сохранений не изменялись."},
    {"Interrupted transfer needs review", "Прерванный перенос требует проверки"},
    {"Saves copied; recovery journal remains", "Сохранения скопированы; журнал восстановления остался"},
    {"Preview is limited to the active campaign ID and does not change files.", "Предпросмотр ограничен ID активной кампании и не изменяет файлы."},
    {"Change the folder above to preview another save namespace.", "Измените папку выше, чтобы просмотреть другое пространство сохранений."},
    {"Preview error: %s", "Ошибка предпросмотра: %s"},
    {"Target: %s", "Назначение: %s"}, {"Slot %d", "Слот %d"},
    {"Slots: %llu valid, %llu recovered, %llu invalid, %llu campaign mismatch; %llu conflicts; %llu bytes.",
        "Слоты: проверено %llu, восстановлено %llu, повреждено %llu, другая кампания %llu; конфликтов %llu; %llu байт."},
    {"Recovered from backup", "Восстановлен из резервной копии"}, {"Campaign ID mismatch", "ID кампании не совпадает"},
    {"Slot", "Слот"}, {"Status", "Состояние"}, {"Level", "Уровень"}, {"Bytes", "Байт"},
    {"Target", "Назначение"}, {"Empty", "Пустой"}, {"Valid", "Проверен"}, {"Invalid", "Ошибка"},
    {"Conflict", "Конфликт"}, {"Clear", "Свободно"},
    {"Unclassified source file: %s", "Неучтённый файл источника: %s"},
    {"Snapshot compatibility still requires the normal campaign load validation.",
        "Совместимость снимка всё ещё требует обычной проверки при загрузке кампании."},
    {"Redo", "Повторить"}, {"Refresh campaigns", "Обновить кампании"}, {"Refresh catalog", "Обновить каталог"},
    {"Refresh level choices", "Обновить список уровней"},
    {"Relative to SDL user-data root; use / separators. Parent paths are rejected.", "Путь относительно каталога данных SDL; используйте /. Пути к родительским каталогам запрещены."},
    {"Remove", "Убрать"}, {"Remove checkpoint", "Удалить контрольную точку"}, {"Remove node", "Удалить узел"},
    {"Remove rule", "Удалить правило"}, {"Remove selected element", "Удалить выбранный элемент"}, {"Rename", "Переименовать"},
    {"Rename %s to %s? This removes the old level file after publishing the new ID. Referencing or unreadable campaign files block the rename; update and save campaign links first. Player progress stays under the old ID and is not moved.", "Переименовать %s в %s? После публикации нового ID старый файл уровня будет удалён. Если на уровень ссылается кампания или файл кампании не читается, переименование запрещено. Сначала обновите и сохраните ссылки кампаний. Прогресс игрока останется под старым ID и не будет перенесён."},
    {"Rename current level", "Переименовать текущий уровень"}, {"Rendered", "С рендерингом"}, {"Repeat", "Повтор"},
    {"Reset", "Сбросить"}, {"Reset debug", "Сбросить отладку"}, {"Reset layout", "Сбросить компоновку"},
    {"Respawn X", "Точка возрождения X"}, {"Respawn Y", "Точка возрождения Y"}, {"Respawn Z", "Точка возрождения Z"},
    {"Responsive layout", "Адаптивная компоновка"}, {"Restart campaign", "Перезапустить кампанию"}, {"Resume", "Продолжить"},
    {"Rotation X", "Вращение X"}, {"Rotation Y", "Вращение Y"}, {"Rotation Z", "Вращение Z"}, {"Run", "Запустить"},
    {"Save campaign", "Сохранить кампанию"}, {"Save campaign…", "Сохранить кампанию…"}, {"Save level", "Сохранить уровень"},
    {"Save profiles", "Сохранить профили"}, {"Save progress", "Сохранить прогресс"}, {"Save Puzzle campaign?", "Сохранить кампанию Puzzle?"},
    {"Save scene", "Сохранить сцену"}, {"Save slot", "Слот сохранения"}, {"Save System", "Система сохранений###SaveSystem"},
    {"SaveProfile ID", "ID профиля сохранения"}, {"SaveProfile version", "Версия профиля сохранения"},
    {"Scene binding", "Привязка к сцене"}, {"Scene file path (UTF-8)", "Путь к файлу сцены (UTF-8)"},
    {"Scene files are stored under Scenes. Import PNG/JPEG source files from Assets/.", "Файлы сцен находятся в Scenes. Исходные PNG/JPEG импортируются из Assets/."},
    {"Scene View", "Окно сцены###SceneView"}, {"Frame All", "Показать все объекты"},
    {"Reload 3D meshes", "Перезагрузить 3D-модели"},
    {"3D preview: %s", "Предпросмотр 3D: %s"},
    {"Right-drag: orbit; middle-drag: pan; wheel: zoom. Frame All fits visible 3D meshes.",
        "Правая кнопка: вращение; средняя: панорамирование; колесо: масштаб. «Показать все» кадрирует видимые 3D-модели."},
    {"Select a Hierarchy entity to edit group membership.", "Выберите сущность в иерархии, чтобы изменить состав группы."},
    {"Select a scene entity to assign explicit fields.", "Выберите сущность сцены, чтобы назначить сохраняемые поля."},
    {"Select a scene entity with Transform to author its checkpoint.", "Выберите сущность сцены с Transform, чтобы настроить контрольную точку."},
    {"Select a valid target entity for this follow preset.", "Выберите подходящую сущность-цель для этого пресета слежения."},
    {"Select an entity in Hierarchy or Scene View.", "Выберите сущность в иерархии или окне сцены."},
    {"Select a scene entity with Transform to author its checkpoint.", "Выберите сущность сцены с Transform, чтобы настроить контрольную точку."},
    {"Settings file needs repair. Apply valid values to replace it: %s", "Файл настроек требует исправления. Укажите допустимые значения и нажмите «Применить»: %s"},
    {"Settings", "Настройки###Settings"}, {"Settings...", "Настройки..."}, {"Language", "Язык"}, {"English", "Английский"}, {"Russian", "Русский"},
    {"New project", "Новый проект"}, {"Open project", "Открыть проект"}, {"New directory (UTF-8)", "Новый каталог (UTF-8)"},
    {"project.astro path (UTF-8)", "Путь к project.astro (UTF-8)"}, {"Create", "Создать"}, {"Open", "Открыть"},
    {"Open Scene", "Открыть сцену"}, {"No project open", "Проект не открыт"}, {"Error: %s", "Ошибка: %s"},
    {"Missing source for AssetId %llu", "Не найден исходный файл ресурса AssetId %llu"}, {"Output: %s", "Выход: %s"},
    {"Apply", "Применить"}, {"Changes apply immediately and are saved for this user.", "Изменения применяются сразу и сохраняются для этого пользователя."},
    {"Could not save language preference: %s", "Не удалось сохранить выбор языка: %s"},
    {"Language preference could not be read; using English.", "Не удалось прочитать настройку языка; используется английский."},
    {"User settings path unavailable; language change cannot be saved.", "Папка настроек пользователя недоступна; выбор языка не сохранён."},
    {"Language preference saved", "Настройка языка сохранена"},
    {"SpriteRenderer", "Отрисовщик спрайтов"}, {"START:", "НАЧАЛО:"}, {"Step", "Шаг"}, {"Stop", "Остановить"},
    {"Subtract", "Вычесть"}, {"Target score", "Целевой счёт"}, {"Test checkpoint activation and restore", "Проверить активацию и восстановление контрольной точки"},
    {"Text", "Текст"}, {"Text is driven by its scene binding.", "Текст задаётся привязкой к сцене."}, {"Texture Asset", "Текстурный ресурс"},
    {"That swap made no match.", "После обмена совпадений не появилось."}, {"Theme", "Тема"}, {"This element has no supported binding target for this entity.", "Для этой сущности у элемента нет поддерживаемой цели привязки."},
    {"Tile kinds", "Типы плиток"}, {"Time", "Время"}, {"Timeline", "Шкала времени###Timeline"},
    {"Transform data is stored; this view remains structural and has no spatial rendering.", "Данные Transform сохраняются; это окно показывает структуру без пространственного рендеринга."},
    {"Transitions:", "Переходы:"}, {"Typed dataflow prototype. Inputs connect through typed menus; graph is not saved yet.", "Прототип типизированного графа данных. Входы соединяются через меню; граф пока не сохраняется."},
    {"UI Editor", "Редактор интерфейса###UIEditor"}, {"UI layout is an in-memory editor document in 06.2.", "Макет интерфейса существует только в памяти редактора."},
    {"Undo", "Отменить"}, {"Up", "Вверх"}, {"Use selected Hierarchy entity", "Использовать выбранную сущность из иерархии"},
    {"User-data subdirectory", "Подкаталог пользовательских данных"}, {"Value", "Значение"}, {"View", "Вид"},
    {"Visual Logic", "Визуальная логика###VisualLogic"}, {"Width", "Ширина"},
    {"XY preview only; does not move Scene entities or render gameplay triggers.", "Только XY-предпросмотр; сущности сцены не перемещаются, игровые триггеры не отображаются."},
    {"Zoom", "Масштаб"},
    {"%llu — %s (%u x %u)", "%llu — %s (%u × %u)"}, {"%s  %.2f s", "%s  %.2f с"},
    {"%s — %s", "%s — %s"}, {"%s — DEFAULT → %s", "%s — ПО УМОЛЧАНИЮ → %s"},
    {"%s — hierarchy preview", "%s — предпросмотр иерархии"}, {"%s — invalid: %s", "%s — ошибка: %s"},
    {"%s — score ≥ %llu → %s", "%s — счёт ≥ %llu → %s"}, {"%s: %llu", "%s: %llu"},
    {"%s: %s", "%s: %s"}, {"%zu levels — links OK", "Уровней: %zu — ссылки в порядке"},
    {"+ Button", "+ Кнопка"}, {"+ Child", "+ Дочерний"}, {"+ Compare", "+ Сравнение"},
    {"+ Entity", "+ Сущность"}, {"+ Logic", "+ Логика"}, {"+ Math", "+ Математика"},
    {"+ Panel", "+ Панель"}, {"+ Text", "+ Текст"}, {"<unconnected>", "<не подключено>"},
    {"AssetId %llu — %s", "Ресурс %llu — %s"},
    {"Campaign assets: <project>/Puzzle/Campaigns and Puzzle/Levels.", "Файлы кампании: <project>/Puzzle/Campaigns и Puzzle/Levels."},
    {"Campaign: %s   Level: %s   Score: %llu / %llu", "Кампания: %s   Уровень: %s   Счёт: %llu / %llu"},
    {"Checkpoint entity: %llu", "Сущность контрольной точки: %llu"}, {"Debug state: pending", "Состояние отладки: ожидает запуска"},
    {"Debugger: %s", "Отладчик: %s"}, {"DEFAULT", "ПО УМОЛЧАНИЮ"},
    {"Effective selected fields: %zu", "Выбрано полей: %zu"}, {"END", "КОНЕЦ"},
    {"Entity %llu", "Сущность %llu"}, {"Entity %llu — %s", "Сущность %llu — %s"},
    {"Files: <project>/Puzzle/Levels/<ID>.astro-puzzle-level (format v1).", "Файлы: <project>/Puzzle/Levels/<ID>.astro-puzzle-level (формат v1)."},
    {"Framebuffer: %d x %d", "Буфер кадра: %d × %d"}, {"Group rule:", "Правило группы:"},
    {"Node %u — %s", "Узел %u — %s"}, {"Output: %s", "Результат: %s"},
    {"Position: %.2f, %.2f, %.2f", "Позиция: %.2f, %.2f, %.2f"},
    {"Preview — %.0f x %.0f", "Предпросмотр — %.0f × %.0f"}, {"Profile: %s", "Профиль: %s"},
    {"Puzzle   Score: %llu   Level: %llu / %llu", "Puzzle   Счёт: %llu   Уровень: %llu / %llu"},
    {"Renderer: %s", "Рендерер: %s"}, {"Rotation: %.2f, %.2f, %.2f", "Вращение: %.2f, %.2f, %.2f"},
    {"Running authored level '%s'; progress slots are isolated by level ID.", "Запущен уровень «%s»; слоты прогресса разделены по ID уровня."},
    {"Save '%s' to Puzzle/Campaigns/%s. An existing campaign with that ID will be replaced. Changing the ID saves a copy and keeps the old campaign.", "Сохранить «%s» в Puzzle/Campaigns/%s. Существующая кампания с таким ID будет заменена. Новый ID создаст копию, прежняя кампания останется."},
    {"Save folder: %s", "Папка сохранений: %s"}, {"Scalar track — %.3f", "Скалярная дорожка — %.3f"},
    {"Scene: %s%s", "Сцена: %s%s"}, {"Score ≥", "Счёт ≥"}, {"Seed", "Начальное значение"},
    {"Start: %s   Ordered levels: %zu   Transitions: %zu", "Начало: %s   Уровней по порядку: %zu   Переходов: %zu"},
    {"UTF-8, max %zu bytes", "UTF-8, не более %zu байт"}, {"Zoom: %.2f", "Масштаб: %.2f"},
    {"Input %u (%s)", "Вход %u (%s)"}, {"Number", "Число"}, {"Boolean", "Логическое"},
    {"String", "Строка"}, {"Unknown", "Неизвестно"}, {"Greater Than", "Больше"},
    {"Less Than", "Меньше"}, {"Equal Number", "Равно"}, {"idle", "ожидает"},
    {"running", "выполняется"}, {"complete", "завершено"}, {"faulted", "ошибка"},
    {"+ Number", "+ Число"}, {"+ Boolean", "+ Логическое"}, {"+ String", "+ Строка"},
    {"Panel", "Панель"}, {"Button", "Кнопка"}, {"Unnamed level", "Безымянный уровень"},
    {"Remove selected entity from group", "Убрать выбранную сущность из группы"},
    {"Add selected entity to group", "Добавить выбранную сущность в группу"},
    {"Remove field from selected entity", "Убрать поле у выбранной сущности"},
    {"Save field on selected entity", "Сохранять поле выбранной сущности"},
    {"Add group rule", "Добавить правило группы"}, {"Remove group rule", "Удалить правило группы"},
    {"Profile validation: %s", "Проверка профиля: %s"}, {"Respawn only", "Только возрождение"},
    {"Save selected profile", "Сохранить выбранный профиль"}, {"Repeatable", "Повторяемая"},
    {"One shot", "Однократная"}, {"Use profile: %s", "Использовать профиль: %s"},
    {"(select a profile)", "(выберите профиль)"}, {"Apply checkpoint", "Применить контрольную точку"},
    {"Add / validate checkpoint", "Добавить / проверить контрольную точку"}, {"Apply: ", "Применить: "},
    {"Missing texture AssetId %llu", "Не найдена текстура AssetId %llu"},
    {"Image imported, but WIC could not decode it for preview", "Изображение импортировано, но WIC не смог декодировать его для предпросмотра"},
    {"Open a project before saving a scene", "Откройте проект перед сохранением сцены"},
    {"Cannot create Scenes directory: ", "Не удалось создать папку Scenes: "},
    {"Save the current scene before opening another scene", "Сохраните текущую сцену перед открытием другой"},
    {"Save the current scene before creating another scene", "Сохраните текущую сцену перед созданием другой"},
    {"Save the current scene before closing the project", "Сохраните текущую сцену перед закрытием проекта"},
    {"New scene created in memory", "Новая сцена создана в памяти"}, {"Scene saved", "Сцена сохранена"},
    {"Scene opened", "Сцена открыта"}, {"Nothing to undo", "Нечего отменять"}, {"Nothing to redo", "Нечего повторять"},
    {"Undo completed", "Отмена выполнена"}, {"Redo completed", "Повтор выполнен"},
    {"Property applied through entity.set_property", "Свойство изменено командой entity.set_property"},
    {"SpriteRenderer assigned through command API", "SpriteRenderer назначен через API команд"},
    {"Entity created", "Сущность создана"}, {"Child entity created", "Дочерняя сущность создана"},
    {"Entity subtree deleted", "Поддерево сущности удалено"}, {"Create or open a project using File.", "Создайте или откройте проект через меню «Файл»."},
    {"Imported image as AssetId ", "Изображение импортировано как AssetId "},
    {"Puzzle SaveProfile and project save folder settings applied", "Настройки SaveProfile и папки сохранений проекта Puzzle применены"},
    {"Saved Puzzle progress to slot ", "Прогресс Puzzle сохранён в слот "},
    {"Loaded Puzzle progress from slot ", "Прогресс Puzzle загружен из слота "},
    {"Saved campaign progress to slot ", "Прогресс кампании сохранён в слот "},
    {"Loaded campaign progress from slot ", "Прогресс кампании загружен из слота "},
    {"Campaign started at level ", "Кампания началась с уровня "}, {"Loaded campaign level ", "Загружен уровень кампании "},
    {"Campaign complete (END). Final level and score are retained.", "Кампания завершена (КОНЕЦ). Последний уровень и счёт сохранены."},
    {"Loaded level: ", "Загружен уровень: "}, {"Saved level: ", "Уровень сохранён: "},
    {"Project opened: ", "Проект открыт: "}, {"Project created: ", "Проект создан: "},
    {"Create or open a project using File.", "Создайте или откройте проект через меню «Файл»."},
    {"Number", "Число"}, {"Boolean", "Логическое"}, {"String", "Строка"}, {"Unknown", "Неизвестно"},
    {"Greater Than", "Больше"}, {"Less Than", "Меньше"}, {"Equal Number", "Равно"},
    {"Input %u (%s)", "Вход %u (%s)"}, {"Profile validation: %s", "Проверка профиля: %s"},
    {"Respawn only", "Только возрождение"}, {"Save selected profile", "Сохранить выбранный профиль"},
    {"Repeatable", "Повторяемая"}, {"One shot", "Однократная"}, {"Use profile: %s", "Использовать профиль: %s"},
    {"(select a profile)", "(выберите профиль)"}, {"Apply: ", "Применить: "},
    {"Remove selected entity from group", "Убрать выбранную сущность из группы"},
    {"Add selected entity to group", "Добавить выбранную сущность в группу"},
    {"Remove field from selected entity", "Убрать поле у выбранной сущности"},
    {"Save field on selected entity", "Сохранять поле выбранной сущности"},
    {"Add group rule", "Добавить правило группы"}, {"Remove group rule", "Удалить правило группы"},
    {"Apply checkpoint", "Применить контрольную точку"}, {"Add / validate checkpoint", "Добавить / проверить контрольную точку"},
    {"Missing texture AssetId %llu", "Не найдена текстура AssetId %llu"},
    {"Missing source for AssetId %llu", "Не найден исходный файл AssetId %llu"},
    {"Open a project before saving a scene", "Откройте проект перед сохранением сцены"},
    {"Cannot create Scenes directory: ", "Не удалось создать папку Scenes: "},
    {"Save the current scene before opening another scene", "Сохраните текущую сцену перед открытием другой"},
    {"Save the current scene before creating another scene", "Сохраните текущую сцену перед созданием другой"},
    {"Save the current scene before closing the project", "Сохраните текущую сцену перед закрытием проекта"},
    {"Image imported, but WIC could not decode it for preview", "Изображение импортировано, но WIC не смог декодировать его для предпросмотра"},
    {"No project open", "Проект не открыт"}, {"Create or open a project using File.", "Создайте или откройте проект через меню «Файл»."},
    {"Scene saved", "Сцена сохранена"}, {"Scene opened", "Сцена открыта"}, {"Nothing to undo", "Нечего отменять"},
    {"Nothing to redo", "Нечего повторять"}, {"Undo completed", "Отмена выполнена"}, {"Redo completed", "Повтор выполнен"},
    {"Property applied through entity.set_property", "Свойство изменено командой entity.set_property"},
    {"SpriteRenderer assigned through command API", "SpriteRenderer назначен через API команд"},
    {"Entity created", "Сущность создана"}, {"Child entity created", "Дочерняя сущность создана"},
    {"Entity subtree deleted", "Поддерево сущности удалено"}, {"New scene created in memory", "Новая сцена создана в памяти"},
    {"Imported image as AssetId ", "Изображение импортировано как AssetId "},
    {"Puzzle SaveProfile and project save folder settings applied", "Настройки SaveProfile и папки сохранений проекта Puzzle применены"},
    {"Saved Puzzle progress to slot ", "Прогресс Puzzle сохранён в слот "},
    {"Loaded Puzzle progress from slot ", "Прогресс Puzzle загружен из слота "},
    {"Campaign started at level ", "Кампания началась с уровня "}, {"Loaded campaign level ", "Загружен уровень кампании "},
    {"Campaign complete (END). Final level and score are retained.", "Кампания завершена (КОНЕЦ). Последний уровень и счёт сохранены."},
    {"Loaded level: ", "Загружен уровень: "}, {"Saved level: ", "Уровень сохранён: "},
    {"Project opened: ", "Проект открыт: "}, {"Project created: ", "Проект создан: "},
    {"Puzzle project settings need repair: ", "Настройки проекта Puzzle требуют исправления: "},
    {"Loaded campaign draft: ", "Загружен черновик кампании: "}, {"New campaign draft", "Новый черновик кампании"},
    {"Change START and incoming targets before removing this level.", "Перед удалением уровня измените START и входящие переходы."},
    {"Saved campaign: ", "Кампания сохранена: "},
    {"C# Play approval###CSharpPlayApproval", "Подтверждение C# перед Play###CSharpPlayApproval"},
    {"Open a project and register a current C# build before Play approval.", "Откройте проект и зарегистрируйте актуальную сборку C# перед подтверждением Play."},
    {"Project: %s", "Проект: %s"}, {"Scene: %s (ID %llu)", "Сцена: %s (ID %llu)"},
    {"Scene file: %s", "Файл сцены: %s"}, {"Build fingerprint: %s", "Отпечаток сборки: %s"},
    {"SDK: %s", "Версия SDK: %s"}, {"Assembly: %s", "Сборка: %s"},
    {"Assembly SHA-256: %s", "SHA-256 сборки: %s"}, {"Runtime config: %s", "Конфигурация runtime: %s"},
    {"Runtime config SHA-256: %s", "SHA-256 конфигурации runtime: %s"},
    {"Enabled ScriptBehavior entries: %llu", "Включённых компонентов ScriptBehavior: %llu"},
    {"Entity %llu, component %llu, AssetId %llu", "Сущность %llu, компонент %llu, AssetId %llu"},
    {"Source: %s", "Исходник: %s"}, {"Managed type: %s", "Тип C#: %s"},
    {"The selected C# script binding is no longer current; refresh and register the build again",
        "Выбранная привязка C# устарела; обновите состояние и повторно зарегистрируйте сборку"},
    {"Enable requires a current registered C# build for this script",
        "Для включения скрипта нужна актуальная зарегистрированная сборка C#"},
    {"Source SHA-256: %s", "SHA-256 исходника: %s"},
    {"C# scripts do not execute in this build. This approval only records your choice for this isolated Play session.", "В этой сборке C#-скрипты не выполняются. Подтверждение лишь запоминает ваш выбор для этой изолированной Play-сессии."},
    {"Approval check failed: %s", "Проверка подтверждения не пройдена: %s"},
    {"Allow for this Play session", "Разрешить для этой Play-сессии"},
    {"This Editor build has no C# runtime support.", "В этой сборке Editor нет поддержки C# runtime."},
    {"C# scripts run in-process with current user privileges; they are not sandboxed.", "C#-скрипты выполняются в процессе с правами текущего пользователя и не изолированы песочницей."},
    {"Approved C# callbacks are active in this Play session.", "Подтверждённые вызовы C# выполняются в этой Play-сессии."},
};

core::Error localizationError(const core::ErrorCode code, std::string message)
{
    return {code, std::move(message)};
}
}

const char* languageName(const EditorLanguage language) noexcept
{
    return language == EditorLanguage::Russian ? "Russian" : "English";
}

const char* languageCode(const EditorLanguage language) noexcept
{
    return language == EditorLanguage::Russian ? "ru" : "en";
}

const char* translateEditorText(const char* source, const EditorLanguage language) noexcept
{
    if (!source || language == EditorLanguage::English) return source ? source : "";
    for (const auto& entry : entries)
        if (std::string_view(source) == entry.english) return entry.russian;
    return source;
}

std::string translateEditorDiagnostic(const std::string_view source, const EditorLanguage language)
{
    if (language == EditorLanguage::English) return std::string(source);
    for (const auto& entry : entries)
        if (entry.english == source) return entry.russian;
    // Detect an already localized message at its start; a Cyrillic file path later in an
    // English diagnostic must not suppress the localized diagnostic prefix.
    for (const auto character : source)
    {
        if (character == ' ' || character == '\t' || character == '\r' || character == '\n') continue;
        if (character == static_cast<char>(0xD0) || character == static_cast<char>(0xD1))
            return std::string(source);
        break;
    }
    return "Ошибка редактора: " + std::string(source);
}

core::Result<EditorLanguage> readEditorLanguage(const std::filesystem::path& settingsPath)
{
    std::error_code ec;
    const bool exists = std::filesystem::exists(settingsPath, ec);
    if (ec) return core::Result<EditorLanguage>::failure(localizationError(core::ErrorCode::IoError,
        "Could not inspect editor language settings: " + ec.message()));
    if (!exists) return core::Result<EditorLanguage>::success(EditorLanguage::English);
    std::ifstream input(settingsPath, std::ios::binary);
    if (!input) return core::Result<EditorLanguage>::failure(localizationError(core::ErrorCode::IoError,
        "Could not open editor language settings"));
    std::string content(128, '\0');
    input.read(content.data(), static_cast<std::streamsize>(content.size()));
    const auto count = static_cast<std::size_t>(input.gcount());
    if (input.bad() || count == content.size())
        return core::Result<EditorLanguage>::failure(localizationError(core::ErrorCode::ValidationError,
            "Editor language settings exceed the supported size"));
    content.resize(count);
    if (content == "ASTRO_EDITOR_SETTINGS 1\nLANGUAGE en\n")
        return core::Result<EditorLanguage>::success(EditorLanguage::English);
    if (content == "ASTRO_EDITOR_SETTINGS 1\nLANGUAGE ru\n")
        return core::Result<EditorLanguage>::success(EditorLanguage::Russian);
    return core::Result<EditorLanguage>::failure(localizationError(core::ErrorCode::ValidationError,
        "Editor language settings have an unsupported format or language"));
}

core::Result<void> writeEditorLanguage(const std::filesystem::path& settingsPath, const EditorLanguage language)
{
    if (language != EditorLanguage::English && language != EditorLanguage::Russian)
        return core::Result<void>::failure(localizationError(core::ErrorCode::ValidationError,
            "Unsupported editor language"));
    std::error_code ec;
    std::filesystem::create_directories(settingsPath.parent_path(), ec);
    if (ec) return core::Result<void>::failure(localizationError(core::ErrorCode::IoError,
        "Could not create editor settings directory: " + ec.message()));
    auto temporary = settingsPath;
    temporary += L".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) return core::Result<void>::failure(localizationError(core::ErrorCode::IoError,
            "Could not create temporary editor settings file"));
        output << "ASTRO_EDITOR_SETTINGS 1\nLANGUAGE " << languageCode(language) << '\n';
        output.flush();
        if (!output)
        {
            std::filesystem::remove(temporary, ec);
            return core::Result<void>::failure(localizationError(core::ErrorCode::IoError,
                "Could not write editor language settings"));
        }
    }
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), settingsPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        const auto error = std::error_code(static_cast<int>(GetLastError()), std::system_category()).message();
        std::filesystem::remove(temporary, ec);
        return core::Result<void>::failure(localizationError(core::ErrorCode::IoError,
            "Could not publish editor language settings: " + error));
    }
#else
    std::filesystem::rename(temporary, settingsPath, ec);
    if (ec)
    {
        std::filesystem::remove(temporary, ec);
        return core::Result<void>::failure(localizationError(core::ErrorCode::IoError,
            "Could not publish editor language settings: " + ec.message()));
    }
#endif
    return core::Result<void>::success();
}
}

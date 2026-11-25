#include "Localization.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

namespace
{
int failures = 0;
void check(const bool condition, const char* message)
{
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}

bool hasLetters(const std::string& value)
{
    const std::regex formatSpecifier(R"REGEX(%[-+ #0-9.*hljztL]*[diuoxXfFeEgGaAcspn])REGEX");
    const auto words = std::regex_replace(value, formatSpecifier, "");
    return std::any_of(words.begin(), words.end(), [](const unsigned char ch) {
        return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
    });
}
}

int main()
{
    using astro::editor::EditorLanguage;
    using astro::editor::translateEditorText;
    check(std::string_view(translateEditorText("Hierarchy", EditorLanguage::English)) == "Hierarchy",
        "English uses source text");
    check(std::string_view(translateEditorText("Hierarchy", EditorLanguage::Russian)) == "Иерархия###Hierarchy",
        "Russian translation preserves stable ImGui window identity");
    check(std::string_view(translateEditorText("Proposed SpriteRenderer visibility", EditorLanguage::Russian))
            == "Новая видимость SpriteRenderer"
        && std::string_view(translateEditorText("Preview visibility change", EditorLanguage::Russian))
            == "Предпросмотр видимости"
        && std::string_view(translateEditorText("Sprite visible", EditorLanguage::Russian)) == "Спрайт виден"
        && std::string_view(translateEditorText("Sprite hidden", EditorLanguage::Russian)) == "Спрайт скрыт",
        "AI SpriteRenderer review controls are localized");
    check(std::string_view(translateEditorText("Create 3D primitive", EditorLanguage::Russian))
            == "Создать 3D-примитив"
        && std::string_view(translateEditorText("Primitive entity name", EditorLanguage::Russian))
            == "Имя сущности-примитива"
        && std::string_view(translateEditorText("Preview primitive creation", EditorLanguage::Russian))
            == "Предпросмотр создания примитива"
        && std::string_view(translateEditorText("Use selected entity as parent", EditorLanguage::Russian))
            == "Использовать выбранную сущность как родителя"
        && std::string_view(translateEditorText("Create %s (%s), parent: %s, visible: %s",
            EditorLanguage::Russian)) == "Создать %s (%s), родитель: %s, видимость: %s"
        && std::string_view(translateEditorText("AI primitive created. Undo is available.",
            EditorLanguage::Russian)) == "Примитив ИИ создан. Действие можно отменить.",
        "AI primitive creation review controls are localized");
    check(std::string_view(translateEditorText("Add Camera3D", EditorLanguage::Russian))
            == "Добавить Camera3D"
        && std::string_view(translateEditorText("Preview add Camera3D", EditorLanguage::Russian))
            == "Предпросмотр добавления Camera3D"
        && std::string_view(translateEditorText("Add an inactive perspective Camera3D to this entity.",
            EditorLanguage::Russian)) == "Добавить к этой сущности неактивную перспективную камеру Camera3D."
        && std::string_view(translateEditorText("Camera3D added. Undo is available.", EditorLanguage::Russian))
            == "Camera3D добавлена. Действие можно отменить.",
        "AI Camera3D addition review controls are localized");
    check(std::string_view(translateEditorText("Review Camera3D lens", EditorLanguage::Russian))
            == "Проверка параметров камеры Camera3D"
        && std::string_view(translateEditorText("Proposed vertical FOV (degrees)", EditorLanguage::Russian))
            == "Новое вертикальное поле обзора (градусы)"
        && std::string_view(translateEditorText("Preview Camera3D lens change", EditorLanguage::Russian))
            == "Предпросмотр изменения параметров Camera3D"
        && std::string_view(translateEditorText("Active Camera3D: this lens change will affect Game View.",
            EditorLanguage::Russian)) == "Изменение активной Camera3D повлияет на игровой вид."
        && std::string_view(translateEditorText("Camera3D lens updated. Undo is available.", EditorLanguage::Russian))
            == "Параметры Camera3D изменены. Действие можно отменить.",
        "AI Camera3D lens review controls and impact notice are localized");
    check(std::string_view(translateEditorText("Review active Camera3D", EditorLanguage::Russian))
            == "Выбор активной Camera3D"
        && std::string_view(translateEditorText("Preview activate selected Camera3D", EditorLanguage::Russian))
            == "Предпросмотр активации выбранной Camera3D"
        && std::string_view(translateEditorText("Preview clear active Camera3D", EditorLanguage::Russian))
            == "Предпросмотр отключения активной Camera3D"
        && std::string_view(translateEditorText("Camera selection: %s → %s", EditorLanguage::Russian))
            == "Выбор камеры: %s → %s"
        && std::string_view(translateEditorText(
            "Switching cameras will change Game View after confirmation.", EditorLanguage::Russian))
            == "После подтверждения игровой вид переключится на другую камеру."
        && std::string_view(translateEditorText(
            "Clearing the active camera hides 3D meshes in Edit Mode Game View; Play Mode keeps its runtime scene.",
            EditorLanguage::Russian))
            == "После отключения камеры 3D-модели исчезнут из игрового вида сцены редактирования; режим Play сохранит свою сцену."
        && std::string_view(translateEditorText(
            "Active Camera3D selection updated. Undo is available.", EditorLanguage::Russian))
            == "Активная Camera3D изменена. Действие можно отменить.",
        "AI active Camera3D review controls and impact notices are localized");
    check(std::string_view(translateEditorText("Language", EditorLanguage::Russian)) == "Язык",
        "settings language label is translated");
    check(std::string_view(translateEditorText("Entities reparented: %zu", EditorLanguage::Russian))
            == "Переподчинено сущностей: %zu"
        && std::string_view(translateEditorText("Selected entities already have this parent", EditorLanguage::Russian))
            == "Выбранные сущности уже имеют этого родителя"
        && std::string_view(translateEditorText("Selected: %zu (Ctrl+click to toggle; drag to reparent)",
            EditorLanguage::Russian))
            == "Выбрано: %zu (Ctrl+щелчок — изменить набор; перетащите для переподчинения)"
        && std::string_view(translateEditorText("%zu entities", EditorLanguage::Russian)) == "%zu сущностей",
        "Hierarchy multi-selection hints and group reparent feedback are localized");
    check(std::string_view(translateEditorText("Transform (Local)", EditorLanguage::Russian))
            == "Transform (локальный)"
        && std::string_view(translateEditorText("Mixed values: %s", EditorLanguage::Russian))
            == "Разные значения: %s"
        && std::string_view(translateEditorText("Apply to selected", EditorLanguage::Russian))
            == "Применить к выбранным"
        && std::string_view(translateEditorText("Primary entity details", EditorLanguage::Russian))
            == "Параметры основного объекта",
        "multi-entity Transform Inspector and primary-entity scope are localized");
    check(std::string_view(translateEditorText("Browse...", EditorLanguage::Russian)) == "Обзор..."
        && std::string_view(translateEditorText("File dialog failed: ", EditorLanguage::Russian))
            == "Не удалось открыть системный диалог: "
        && std::string_view(translateEditorText("Selected path is too long for the project path field",
            EditorLanguage::Russian)) == "Выбранный путь не помещается в поле пути проекта",
        "project picker button and diagnostics are localized");
    check(std::string_view(translateEditorText("Project parent directory (UTF-8)", EditorLanguage::Russian))
        == "Родительский каталог проекта (UTF-8)", "project parent directory is localized");
    check(std::string_view(translateEditorText("PrimitiveRenderer", EditorLanguage::Russian))
            == "Компонент PrimitiveRenderer"
        && std::string_view(translateEditorText("Primitive shape", EditorLanguage::Russian))
            == "Форма примитива"
        && std::string_view(translateEditorText("Visible", EditorLanguage::Russian)) == "Видимость"
        && std::string_view(translateEditorText("Primitive settings applied", EditorLanguage::Russian))
            == "Параметры примитива применены",
        "primitive Inspector controls and status are localized");
    check(std::string_view(translateEditorText("PointLight3D", EditorLanguage::Russian))
            == "Точечный свет 3D"
        && std::string_view(translateEditorText("Add PointLight3D", EditorLanguage::Russian))
            == "Добавить PointLight3D"
        && std::string_view(translateEditorText("Linear color (RGB)", EditorLanguage::Russian))
            == "Линейный цвет (RGB)"
        && std::string_view(translateEditorText("Intensity value", EditorLanguage::Russian))
            == "Значение интенсивности"
        && std::string_view(translateEditorText("Apply point light", EditorLanguage::Russian))
            == "Применить параметры света"
        && std::string_view(translateEditorText("Remove PointLight3D", EditorLanguage::Russian))
            == "Удалить PointLight3D",
        "point-light Inspector controls are localized");
    check(std::string_view(translateEditorText("DirectionalLight3D", EditorLanguage::Russian))
            == "Направленный свет 3D"
        && std::string_view(translateEditorText("Add DirectionalLight3D", EditorLanguage::Russian))
            == "Добавить DirectionalLight3D"
        && std::string_view(translateEditorText("Directional light follows local -Z and parent rotations.",
            EditorLanguage::Russian))
            == "Направление — локальная ось -Z с учётом поворотов родителей."
        && std::string_view(translateEditorText("Apply directional light", EditorLanguage::Russian))
            == "Применить параметры направленного света"
        && std::string_view(translateEditorText("Remove DirectionalLight3D", EditorLanguage::Russian))
            == "Удалить DirectionalLight3D",
        "directional-light Inspector controls are localized");
    check(std::string_view(translateEditorText("MeshRenderer material", EditorLanguage::Russian))
            == "Материал MeshRenderer"
        && std::string_view(translateEditorText("Source glTF material index: %u", EditorLanguage::Russian))
            == "Индекс исходного материала glTF: %u",
        "material Inspector labels and formatted source-slot index are translated");
    check(astro::editor::translateEditorDiagnostic(
            "Base-color texture needs a registered image and UV0; using vertex-color fallback",
            EditorLanguage::Russian)
            == "Текстуре base color нужны зарегистрированное изображение и UV0; используется резервный vertex color"
        && astro::editor::translateEditorDiagnostic("MeshRenderer model AssetId is not registered", EditorLanguage::Russian)
            == "AssetId модели MeshRenderer не зарегистрирован",
        "Scene View material fallback and missing-mesh diagnostics are localized");
    check(astro::editor::translateEditorDiagnostic(
            "Game View has no active Camera3D; 3D meshes were skipped", EditorLanguage::Russian)
            == "В игровом виде не выбрана активная камера 3D; 3D-модели не отображаются"
        && astro::editor::translateEditorDiagnostic(
            "Scene Transform is outside the camera/render range", EditorLanguage::Russian)
            == "Transform сцены вне диапазона камеры или рендерера"
        && std::string_view(translateEditorText("Game View diagnostic: ", EditorLanguage::Russian))
            == "Диагностика игрового вида: "
        && std::string_view(translateEditorText("Game View diagnostics", EditorLanguage::Russian))
            == "Диагностики игрового вида"
        && std::string_view(translateEditorText("Select entity in Hierarchy", EditorLanguage::Russian))
            == "Выбрать сущность в иерархии"
        && std::string_view(translateEditorText("Related entity: %s", EditorLanguage::Russian))
            == "Связанная сущность: %s"
        && std::string_view(translateEditorText("Related asset: %s (AssetId %llu)", EditorLanguage::Russian))
            == "Связанный ресурс: %s (AssetId %llu)"
        && std::string_view(translateEditorText("Select asset in Project", EditorLanguage::Russian))
            == "Выбрать ресурс в окне проекта"
        && std::string_view(translateEditorText("Selected registered asset", EditorLanguage::Russian))
            == "Выбранный зарегистрированный ресурс"
        && std::string_view(translateEditorText("Asset type: %s", EditorLanguage::Russian))
            == "Тип ресурса: %s"
        && std::string_view(translateEditorText("Material", EditorLanguage::Russian))
            == "Материал"
        && std::string_view(translateEditorText("Asset Browser", EditorLanguage::Russian))
            == "Браузер ресурсов"
        && std::string_view(translateEditorText("Search by path or AssetId (UTF-8)", EditorLanguage::Russian))
            == "Поиск по пути или AssetId (UTF-8)"
        && std::string_view(translateEditorText("Scripts", EditorLanguage::Russian)) == "Скрипты"
        && std::string_view(translateEditorText("Camera candidates:", EditorLanguage::Russian))
            == "Другие камеры сцены:"
        && std::string_view(translateEditorText("No Camera3D components in this scene.", EditorLanguage::Russian))
            == "В этой сцене нет компонентов Camera3D.",
        "Game View camera diagnostics and their transform cause are localized");
    check(astro::editor::translateEditorDiagnostic(
            "Source base-color image is missing or not a local external file; using vertex-color fallback",
            EditorLanguage::Russian)
            == "Исходное base-color изображение отсутствует или не является локальным файлом; используется резервный vertex color"
        && astro::editor::translateEditorDiagnostic("Base-color texture requires UV0; using vertex-color fallback",
            EditorLanguage::Russian) == "Для base-color текстуры нужен UV0; используется резервный vertex color",
        "source-material and UV fallback diagnostics are localized");
    check(astro::editor::translateEditorDiagnostic(
            "Source base-color sampler requests mip filtering; this preview has no mip chain, using vertex-color fallback",
            EditorLanguage::Russian)
            == "Исходный base-color sampler требует mip-фильтрацию, которой нет в предпросмотре; используются цвета вершин"
        && astro::editor::translateEditorDiagnostic(
            "Source base-color UV transform is invalid; using vertex-color fallback", EditorLanguage::Russian)
            == "UV-преобразование исходной base-color текстуры некорректно; используются цвета вершин",
        "new sampler and UV transform fallback diagnostics are localized");
    check(std::string_view(translateEditorText(
            "Scene View preview source: project material override", EditorLanguage::Russian))
            == "Источник предпросмотра окна сцены: материал проекта",
        "project material preview source is localized");
    check(std::string_view(translateEditorText(
            "Scene View preview source: glTF source material", EditorLanguage::Russian))
            == "Источник предпросмотра окна сцены: исходный материал glTF",
        "glTF source material preview source is localized");
    check(std::string_view(translateEditorText(
            "Clear the override to preview the glTF source material.", EditorLanguage::Russian))
            == "Сбросьте override, чтобы просматривать исходный материал glTF.",
        "Inspector source-material selection hint is localized");
    check(std::string_view(translateEditorText(
            "Scene View preview source: imported vertex colors", EditorLanguage::Russian))
            == "Источник предпросмотра окна сцены: цвета вершин модели",
        "vertex-color preview source is localized");
    check(std::string_view(translateEditorText(
            "Scene View preview renders base-color factor and a local UV0 texture.", EditorLanguage::Russian))
            == "Предпросмотр окна сцены отображает base-color factor и локальную текстуру UV0.",
        "supported material preview features are localized");
    check(std::string_view(translateEditorText(
            "Source glTF sampler and UV transform apply on UV0; mip filters use generated mip levels.", EditorLanguage::Russian))
            == "Sampler и UV-преобразование исходного glTF применяются к UV0; mip-фильтры используют созданные mip-уровни.",
        "source sampler, transform and mip support are localized");
    check(std::string_view(translateEditorText(
            "Alpha and non-base-color PBR properties are not rendered by this preview.", EditorLanguage::Russian))
            == "Этот предпросмотр не отображает alpha и свойства PBR, кроме base-color.",
        "unsupported material preview features are localized");
    check(std::string_view(translateEditorText(
            "Scene View preview: unresolved override; vertex-color fallback", EditorLanguage::Russian))
            == "Предпросмотр окна сцены: override не разрешён; используются цвета вершин",
        "invalid override preview fallback is localized");
    check(std::string_view(translateEditorText("Campaign save slot", EditorLanguage::Russian))
            == "Слот сохранения кампании",
        "campaign slot selector is translated");
    check(std::string_view(translateEditorText("Saved campaign progress to slot ", EditorLanguage::Russian))
            == "Прогресс кампании сохранён в слот ",
        "campaign save confirmation is translated");
    check(std::string_view(translateEditorText("Loaded campaign progress from slot ", EditorLanguage::Russian))
            == "Прогресс кампании загружен из слота ",
        "campaign load confirmation is translated");
    check(astro::editor::translateEditorDiagnostic("No errors recorded", EditorLanguage::Russian) == "Ошибок нет",
        "known diagnostic is translated as a whole");
    check(astro::editor::translateEditorDiagnostic(
            "Registered managed type differs from the current build binding; register the current build",
            EditorLanguage::Russian)
            == "Зарегистрированный managed type не совпадает с привязкой текущей сборки; зарегистрируйте сборку заново",
        "managed type mismatch diagnostic is translated");
    check(astro::editor::translateEditorDiagnostic("asset source unavailable", EditorLanguage::Russian)
        == "Ошибка редактора: asset source unavailable",
        "unknown diagnostic is clearly marked in Russian while preserving source detail");
    check(astro::editor::translateEditorDiagnostic("Ошибка уже локализована", EditorLanguage::Russian)
        == "Ошибка уже локализована", "already localized diagnostics are not prefixed twice");
    check(astro::editor::translateEditorDiagnostic("file not found: C:\\проект\\scene.astro", EditorLanguage::Russian)
        == "Ошибка редактора: file not found: C:\\проект\\scene.astro",
        "Unicode paths do not suppress the localized diagnostic prefix");

    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto temporary = std::filesystem::temp_directory_path() / ("astro-localization-test-" + std::to_string(unique));
    const auto settingsPath = temporary / "editor-settings.cfg";
    const auto missing = astro::editor::readEditorLanguage(settingsPath);
    check(missing && missing.value() == EditorLanguage::English, "missing preference defaults to English");
    const auto savedRussian = astro::editor::writeEditorLanguage(settingsPath, EditorLanguage::Russian);
    const auto loadedRussian = astro::editor::readEditorLanguage(settingsPath);
    check(savedRussian && loadedRussian && loadedRussian.value() == EditorLanguage::Russian,
        "Russian language preference round-trips");
    const auto savedEnglish = astro::editor::writeEditorLanguage(settingsPath, EditorLanguage::English);
    const auto loadedEnglish = astro::editor::readEditorLanguage(settingsPath);
    check(savedEnglish && loadedEnglish && loadedEnglish.value() == EditorLanguage::English,
        "preference can be atomically changed back to English");
    {
        std::ofstream output(settingsPath, std::ios::binary | std::ios::trunc);
        output << "ASTRO_EDITOR_SETTINGS 99\nLANGUAGE xx\n";
    }
    check(!astro::editor::readEditorLanguage(settingsPath), "reject unsupported settings versions/languages");
    check(!astro::editor::writeEditorLanguage(settingsPath, static_cast<EditorLanguage>(99)),
        "reject unsupported language enum without publishing it");

#ifdef ASTRO_EDITOR_MAIN_SOURCE_PATH
    std::ifstream sourceFile(ASTRO_EDITOR_MAIN_SOURCE_PATH, std::ios::binary);
    const std::string source{std::istreambuf_iterator<char>(sourceFile), std::istreambuf_iterator<char>()};
    check(sourceFile.good() || sourceFile.eof(), "read editor UI source for translation coverage");
    const std::regex translatedCall(R"REGEX(ImGui::[A-Za-z_][A-Za-z_0-9]*\(\s*tr\("((?:\\.|[^"\\])*)"\))REGEX");
    std::size_t covered = 0;
    for (std::sregex_iterator it(source.begin(), source.end(), translatedCall), end; it != end; ++it)
    {
        const auto key = (*it)[1].str();
        if (!hasLetters(key)) continue; // Placeholder-only formats such as "%s" contain no UI wording.
        const auto* translated = translateEditorText(key.c_str(), EditorLanguage::Russian);
        if (std::string_view(translated) == key)
        {
            std::cerr << "UNTRANSLATED: " << key << '\n';
            check(false, "every English ImGui label has a Russian translation");
        }
        ++covered;
    }
    check(covered >= 200, "Russian catalog covers the complete editor label set");

    const std::regex directTextLiteral(R"REGEX(ImGui::[A-Za-z_][A-Za-z_0-9]*\(\s*"([^"]*)")REGEX");
    const std::vector<std::string> stableIds{
        "CheckpointMap", "EntityTree", "LogicNodes", "PuzzleBoardInput", "PuzzleCampaignCatalog",
        "PuzzleLevelBoard", "PuzzleLevelCatalog", "SceneCanvas", "SceneViewTabs", "TimelineCurve",
        "UiElementList", "UiInspector", "UiPreview", "UiPreviewCanvas"};
    for (std::sregex_iterator it(source.begin(), source.end(), directTextLiteral), end; it != end; ++it)
    {
        const auto literal = (*it)[1].str();
        const bool hidden = literal.starts_with("##");
        const bool stable = std::find(stableIds.begin(), stableIds.end(), literal) != stableIds.end();
        check(hidden || stable, "remaining raw ImGui strings are hidden IDs, not user-facing text");
    }
#endif

    std::error_code ec;
    std::filesystem::remove_all(temporary, ec);
    check(!ec, "remove isolated localization test directory");
    return failures == 0 ? 0 : 1;
}

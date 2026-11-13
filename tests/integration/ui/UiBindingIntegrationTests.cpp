#include "astro/ui/UiDocument.hpp"

#include <cstdlib>
#include <iostream>

namespace
{
int failures = 0;
template<typename T>
void check(const T& condition, const char* message)
{
    if (!static_cast<bool>(condition)) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
}

int main()
{
    using namespace astro::ui;
    astro::scene::Scene source(7, "Binding source");
    const auto entity = source.createEntity("Player Ω");
    check(entity, "create source entity");
    if (!entity) return EXIT_FAILURE;
    check(source.setSpriteRenderer(entity.value(), astro::assets::AssetId{42}, false),
        "create source visible property");

    UiDocument document;
    const auto text = document.addElement(ElementKind::Text, "Player label");
    const auto icon = document.addElement(ElementKind::Panel, "Player icon");
    check(text && icon, "create UI binding targets");
    if (!text || !icon) return EXIT_FAILURE;
    check(document.setText(text.value(), "Unbound"), "set fallback text");
    check(document.setBinding(text.value(), UiBinding{entity.value(), BindingSource::EntityName, BindingTarget::Text}, source),
        "bind Entity.Name to UI Text");
    check(document.setBinding(icon.value(), UiBinding{entity.value(), BindingSource::SpriteVisible, BindingTarget::Visible}, source),
        "bind SpriteRenderer.Visible to UI visibility");
    check(document.refreshBindings(source), "refresh validated bindings from the scene");
    check(document.find(text.value())->text == "Player Ω", "copy current entity name into UI text");
    check(!document.find(icon.value())->visible, "copy sprite visibility into UI visibility");

    const auto previousText = document.find(text.value())->text;
    check(!document.setBinding(text.value(), UiBinding{entity.value(), BindingSource::EntityName, BindingTarget::Visible}, source),
        "reject incompatible binding without replacing current binding");
    check(document.find(text.value())->binding->source == BindingSource::EntityName,
        "failed binding validation preserves old binding");
    check(source.setProperty(entity.value(), "name", std::string{"Renamed Ω"}), "rename source entity");
    check(document.refreshBindings(source), "refresh after source value changes");
    check(document.find(text.value())->text == "Renamed Ω", "reflect updated name in UI document");

    const auto removed = source.erase(entity.value());
    check(removed, "remove binding source entity");
    const auto refresh = document.refreshBindings(source);
    check(!refresh, "report stale entity binding after deletion");
    check(document.find(text.value())->text == "Renamed Ω", "failed refresh leaves all target values unchanged");
    check(failures == 0, "all UI binding integration checks passed");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

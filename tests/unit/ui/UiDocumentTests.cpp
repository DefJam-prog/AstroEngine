#include "astro/ui/UiDocument.hpp"

#include <cstdlib>
#include <iostream>
#include <limits>

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
    UiDocument document;
    check(document.theme() == ThemeKind::Dark, "default document theme is dark");
    check(document.setTheme(ThemeKind::Light) && document.resolvedTheme().kind == ThemeKind::Light,
        "select a built-in light theme");
    check(document.resolvedTheme().background != Theme{}.background, "light theme changes preview colors");
    check(!document.setTheme(static_cast<ThemeKind>(99)), "reject unknown theme values");

    const auto panel = document.addElement(ElementKind::Panel, "Root panel");
    const auto label = document.addElement(ElementKind::Text, "Title", panel ? std::optional{panel.value()} : std::nullopt);
    check(panel && label, "create root and child UI elements");
    if (!panel || !label) return EXIT_FAILURE;

    ResponsiveRect rootLayout;
    rootLayout.anchorMinX = 0.05;
    rootLayout.anchorMinY = 0.05;
    rootLayout.anchorMaxX = 0.95;
    rootLayout.anchorMaxY = 0.95;
    rootLayout.offsetMaxX = -10.0;
    rootLayout.offsetMaxY = -10.0;
    check(document.setLayout(panel.value(), rootLayout), "set stretch anchors with insets");
    ResponsiveRect childLayout;
    childLayout.anchorMinX = 0.5;
    childLayout.anchorMinY = 0.0;
    childLayout.anchorMaxX = 1.0;
    childLayout.anchorMaxY = 0.0;
    childLayout.offsetMinX = 5.0;
    childLayout.offsetMinY = 12.0;
    childLayout.offsetMaxX = -5.0;
    childLayout.offsetMaxY = 44.0;
    check(document.setLayout(label.value(), childLayout), "set child responsive layout");
    const auto wide = document.resolveLayout(label.value(), 1280, 720);
    const auto narrow = document.resolveLayout(label.value(), 390, 844);
    check(wide && narrow && wide.value().width > narrow.value().width,
        "resolve anchored child against two viewport sizes");
    check(!document.resolveLayout(label.value(), 0, 720), "reject zero-size viewport");

    auto invalidLayout = childLayout;
    invalidLayout.anchorMinX = 1.5;
    check(!document.setLayout(label.value(), invalidLayout), "reject out-of-range normalized anchors");
    invalidLayout = childLayout;
    invalidLayout.offsetMaxY = std::numeric_limits<double>::infinity();
    check(!document.setLayout(label.value(), invalidLayout), "reject non-finite layout offsets");
    invalidLayout = childLayout;
    invalidLayout.offsetMaxY = 1000001.0;
    check(!document.setLayout(label.value(), invalidLayout), "reject unsupported large offsets");
    check(!document.resolveLayout(label.value(), 70000, 720), "reject unsupported large preview viewports");
    check(document.setText(label.value(), "Заголовок Ω"), "accept UTF-8 UI text");
    check(document.setText(label.value(), ""), "allow intentionally empty UI text");
    check(!document.setText(label.value(), std::string(4097, 'x')), "enforce the UI text byte limit");
    check(!document.addElement(ElementKind::Text, std::string("bad\xC0\xAF", 5)), "reject invalid UTF-8 names");
    check(!document.removeElement(panel.value()), "protect parent from deletion while child exists");
    check(document.removeElement(label.value()) && document.removeElement(panel.value()), "remove leaf then parent");
    check(!document.removeElement(label.value()), "report missing element IDs");
    check(failures == 0, "all UI document unit checks passed");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

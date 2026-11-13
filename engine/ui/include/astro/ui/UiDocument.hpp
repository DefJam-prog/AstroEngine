#pragma once

#include "astro/core/Result.hpp"
#include "astro/scene/Scene.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace astro::ui
{
using ElementId = std::uint64_t;

enum class ElementKind : std::uint8_t { Panel, Text, Button };
enum class ThemeKind : std::uint8_t { Dark, Light };
enum class BindingSource : std::uint8_t { EntityName, SpriteVisible };
enum class BindingTarget : std::uint8_t { Text, Visible };

struct ResponsiveRect
{
    double anchorMinX = 0.0;
    double anchorMinY = 0.0;
    double anchorMaxX = 0.0;
    double anchorMaxY = 0.0;
    double offsetMinX = 0.0;
    double offsetMinY = 0.0;
    double offsetMaxX = 240.0;
    double offsetMaxY = 48.0;
    friend bool operator==(const ResponsiveRect&, const ResponsiveRect&) = default;
};

struct ResolvedRect
{
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
    friend bool operator==(const ResolvedRect&, const ResolvedRect&) = default;
};

struct Theme
{
    ThemeKind kind = ThemeKind::Dark;
    std::uint32_t background = 0x20242AFF;
    std::uint32_t surface = 0x343B46FF;
    std::uint32_t text = 0xF2F4F8FF;
    std::uint32_t accent = 0x5B8DEEFF;
    double spacing = 8.0;
};

struct UiBinding
{
    scene::EntityId entity = 0;
    BindingSource source = BindingSource::EntityName;
    BindingTarget target = BindingTarget::Text;
    friend bool operator==(const UiBinding&, const UiBinding&) = default;
};

struct UiElement
{
    ElementId id = 0;
    ElementKind kind = ElementKind::Text;
    std::optional<ElementId> parent;
    std::string name;
    std::string text;
    bool visible = true;
    ResponsiveRect layout{};
    std::optional<UiBinding> binding;
};

class UiDocument final
{
public:
    [[nodiscard]] const std::vector<UiElement>& elements() const noexcept { return elements_; }
    [[nodiscard]] const UiElement* find(ElementId id) const noexcept;
    [[nodiscard]] core::Result<void> setText(ElementId id, std::string text);
    [[nodiscard]] ThemeKind theme() const noexcept { return theme_; }
    [[nodiscard]] core::Result<void> setTheme(ThemeKind theme);
    [[nodiscard]] Theme resolvedTheme() const noexcept;
    [[nodiscard]] core::Result<ElementId> addElement(ElementKind kind, std::string name,
        std::optional<ElementId> parent = std::nullopt);
    [[nodiscard]] core::Result<void> removeElement(ElementId id);
    [[nodiscard]] core::Result<void> setLayout(ElementId id, const ResponsiveRect& layout);
    [[nodiscard]] core::Result<ResolvedRect> resolveLayout(ElementId id, double viewportWidth,
        double viewportHeight) const;
    [[nodiscard]] core::Result<void> setBinding(ElementId id, std::optional<UiBinding> binding,
        const scene::Scene& sourceScene);
    [[nodiscard]] core::Result<void> refreshBindings(const scene::Scene& sourceScene);

private:
    [[nodiscard]] static core::Result<void> validateLayout(const ResponsiveRect& layout);
    [[nodiscard]] static core::Result<void> validateBinding(const UiElement& element,
        const UiBinding& binding, const scene::Scene& sourceScene);

    std::vector<UiElement> elements_;
    ElementId nextId_ = 1;
    ThemeKind theme_ = ThemeKind::Dark;
};

[[nodiscard]] std::string_view elementKindName(ElementKind kind) noexcept;
[[nodiscard]] std::string_view bindingSourceName(BindingSource source) noexcept;
[[nodiscard]] std::string_view bindingTargetName(BindingTarget target) noexcept;
}

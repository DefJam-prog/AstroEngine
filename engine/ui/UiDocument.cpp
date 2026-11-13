#include "astro/ui/UiDocument.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <utility>

namespace astro::ui
{
namespace
{
core::Error uiError(const core::ErrorCode code, std::string message)
{
    return {code, std::move(message)};
}

bool validKind(const ElementKind kind) noexcept
{
    return kind == ElementKind::Panel || kind == ElementKind::Text || kind == ElementKind::Button;
}

bool validUtf8Text(const std::string_view text, const std::size_t maximumBytes)
{
    if (text.size() > maximumBytes || text.find('\0') != std::string_view::npos) return false;
    std::size_t i = 0;
    while (i < text.size())
    {
        const auto first = static_cast<unsigned char>(text[i]);
        if (first <= 0x7F) { ++i; continue; }
        std::size_t count = 0;
        std::uint32_t code = 0;
        if ((first & 0xE0) == 0xC0) { count = 2; code = first & 0x1F; }
        else if ((first & 0xF0) == 0xE0) { count = 3; code = first & 0x0F; }
        else if ((first & 0xF8) == 0xF0) { count = 4; code = first & 0x07; }
        else return false;
        if (i + count > text.size()) return false;
        for (std::size_t j = 1; j < count; ++j)
        {
            const auto continuation = static_cast<unsigned char>(text[i + j]);
            if ((continuation & 0xC0) != 0x80) return false;
            code = (code << 6) | (continuation & 0x3F);
        }
        if ((count == 2 && code < 0x80) || (count == 3 && code < 0x800)
            || (count == 4 && code < 0x10000) || code > 0x10FFFF
            || (code >= 0xD800 && code <= 0xDFFF)) return false;
        i += count;
    }
    return true;
}
}

const UiElement* UiDocument::find(const ElementId id) const noexcept
{
    const auto found = std::find_if(elements_.begin(), elements_.end(), [id](const UiElement& element) {
        return element.id == id;
    });
    return found == elements_.end() ? nullptr : &*found;
}

core::Result<void> UiDocument::setText(const ElementId id, std::string text)
{
    const auto found = std::find_if(elements_.begin(), elements_.end(), [id](const UiElement& element) {
        return element.id == id;
    });
    if (found == elements_.end())
        return core::Result<void>::failure(uiError(core::ErrorCode::NotFound, "UI element was not found"));
    if (!validUtf8Text(text, 4096))
        return core::Result<void>::failure(uiError(core::ErrorCode::ValidationError,
            "UI text must be valid non-empty UTF-8 up to 4096 bytes"));
    found->text = std::move(text);
    return core::Result<void>::success();
}

core::Result<void> UiDocument::setTheme(const ThemeKind theme)
{
    if (theme != ThemeKind::Dark && theme != ThemeKind::Light)
        return core::Result<void>::failure(uiError(core::ErrorCode::ValidationError, "Unknown UI theme"));
    theme_ = theme;
    return core::Result<void>::success();
}

Theme UiDocument::resolvedTheme() const noexcept
{
    if (theme_ == ThemeKind::Light)
        return {ThemeKind::Light, 0xE8ECF2FF, 0xFFFFFFFF, 0x202633FF, 0x2868C7FF, 8.0};
    return {};
}

core::Result<ElementId> UiDocument::addElement(const ElementKind kind, std::string name,
    const std::optional<ElementId> parent)
{
    if (!validKind(kind) || name.empty() || !validUtf8Text(name, 120))
        return core::Result<ElementId>::failure(uiError(core::ErrorCode::ValidationError,
            "UI element kind or UTF-8 name is invalid"));
    if (parent && !find(*parent))
        return core::Result<ElementId>::failure(uiError(core::ErrorCode::NotFound, "UI parent element was not found"));
    if (elements_.size() >= 512 || nextId_ == std::numeric_limits<ElementId>::max())
        return core::Result<ElementId>::failure(uiError(core::ErrorCode::ValidationError, "UI element limit reached"));
    const auto id = nextId_++;
    elements_.push_back({id, kind, parent, std::move(name), {}, true, {}, std::nullopt});
    return core::Result<ElementId>::success(id);
}

core::Result<void> UiDocument::removeElement(const ElementId id)
{
    const auto found = std::find_if(elements_.begin(), elements_.end(), [id](const UiElement& element) {
        return element.id == id;
    });
    if (found == elements_.end())
        return core::Result<void>::failure(uiError(core::ErrorCode::NotFound, "UI element was not found"));
    if (std::any_of(elements_.begin(), elements_.end(), [id](const UiElement& element) {
        return element.parent == id;
    }))
        return core::Result<void>::failure(uiError(core::ErrorCode::InvalidState,
            "Remove child UI elements before their parent"));
    elements_.erase(found);
    return core::Result<void>::success();
}

core::Result<void> UiDocument::validateLayout(const ResponsiveRect& layout)
{
    const double values[]{layout.anchorMinX, layout.anchorMinY, layout.anchorMaxX, layout.anchorMaxY,
        layout.offsetMinX, layout.offsetMinY, layout.offsetMaxX, layout.offsetMaxY};
    if (std::any_of(std::begin(values), std::end(values), [](const double value) { return !std::isfinite(value); }))
        return core::Result<void>::failure(uiError(core::ErrorCode::ValidationError,
            "UI layout values must be finite"));
    if (layout.anchorMinX < 0.0 || layout.anchorMinX > 1.0 || layout.anchorMaxX < 0.0 || layout.anchorMaxX > 1.0
        || layout.anchorMinY < 0.0 || layout.anchorMinY > 1.0 || layout.anchorMaxY < 0.0 || layout.anchorMaxY > 1.0
        || layout.anchorMinX > layout.anchorMaxX || layout.anchorMinY > layout.anchorMaxY
        || std::abs(layout.offsetMinX) > 1000000.0 || std::abs(layout.offsetMinY) > 1000000.0
        || std::abs(layout.offsetMaxX) > 1000000.0 || std::abs(layout.offsetMaxY) > 1000000.0)
        return core::Result<void>::failure(uiError(core::ErrorCode::ValidationError,
            "UI anchors must be ordered in [0,1] and offsets must be within one million pixels"));
    return core::Result<void>::success();
}

core::Result<void> UiDocument::setLayout(const ElementId id, const ResponsiveRect& layout)
{
    const auto found = std::find_if(elements_.begin(), elements_.end(), [id](const UiElement& element) {
        return element.id == id;
    });
    if (found == elements_.end())
        return core::Result<void>::failure(uiError(core::ErrorCode::NotFound, "UI element was not found"));
    const auto valid = validateLayout(layout);
    if (!valid) return valid;
    found->layout = layout;
    return core::Result<void>::success();
}

core::Result<ResolvedRect> UiDocument::resolveLayout(const ElementId id, const double viewportWidth,
    const double viewportHeight) const
{
    if (!std::isfinite(viewportWidth) || !std::isfinite(viewportHeight) || viewportWidth <= 0.0 || viewportHeight <= 0.0
        || viewportWidth > 65536.0 || viewportHeight > 65536.0)
        return core::Result<ResolvedRect>::failure(uiError(core::ErrorCode::ValidationError,
            "UI preview viewport dimensions must be finite, positive, and at most 65536 pixels"));
    std::function<core::Result<ResolvedRect>(ElementId, std::size_t)> resolve;
    resolve = [&](const ElementId current, const std::size_t depth) -> core::Result<ResolvedRect> {
        if (depth > elements_.size())
            return core::Result<ResolvedRect>::failure(uiError(core::ErrorCode::InvalidState, "UI hierarchy contains a cycle"));
        const auto* element = find(current);
        if (!element)
            return core::Result<ResolvedRect>::failure(uiError(core::ErrorCode::NotFound, "UI element was not found"));
        ResolvedRect parent{0.0, 0.0, viewportWidth, viewportHeight};
        if (element->parent)
        {
            const auto resolvedParent = resolve(*element->parent, depth + 1);
            if (!resolvedParent) return resolvedParent;
            parent = resolvedParent.value();
        }
        const auto& layout = element->layout;
        const double left = parent.x + layout.anchorMinX * parent.width + layout.offsetMinX;
        const double top = parent.y + layout.anchorMinY * parent.height + layout.offsetMinY;
        const double right = parent.x + layout.anchorMaxX * parent.width + layout.offsetMaxX;
        const double bottom = parent.y + layout.anchorMaxY * parent.height + layout.offsetMaxY;
        if (!std::isfinite(left) || !std::isfinite(top) || !std::isfinite(right) || !std::isfinite(bottom)
            || right < left || bottom < top)
            return core::Result<ResolvedRect>::failure(uiError(core::ErrorCode::ValidationError,
                "Resolved UI element has a negative or non-finite size"));
        return core::Result<ResolvedRect>::success({left, top, right - left, bottom - top});
    };
    return resolve(id, 0);
}

core::Result<void> UiDocument::validateBinding(const UiElement& element, const UiBinding& binding,
    const scene::Scene& sourceScene)
{
    const auto* entity = sourceScene.find(binding.entity);
    if (!entity)
        return core::Result<void>::failure(uiError(core::ErrorCode::NotFound, "UI binding entity does not exist"));
    const bool compatible = (binding.source == BindingSource::EntityName && binding.target == BindingTarget::Text
            && (element.kind == ElementKind::Text || element.kind == ElementKind::Button))
        || (binding.source == BindingSource::SpriteVisible && binding.target == BindingTarget::Visible
            && sourceScene.spriteRenderer(binding.entity) != nullptr);
    if (!compatible)
        return core::Result<void>::failure(uiError(core::ErrorCode::ValidationError,
            "UI binding source, target, or element kind is incompatible"));
    return core::Result<void>::success();
}

core::Result<void> UiDocument::setBinding(const ElementId id, std::optional<UiBinding> binding,
    const scene::Scene& sourceScene)
{
    const auto found = std::find_if(elements_.begin(), elements_.end(), [id](const UiElement& element) {
        return element.id == id;
    });
    if (found == elements_.end())
        return core::Result<void>::failure(uiError(core::ErrorCode::NotFound, "UI element was not found"));
    if (binding)
    {
        const auto valid = validateBinding(*found, *binding, sourceScene);
        if (!valid) return valid;
    }
    found->binding = std::move(binding);
    return core::Result<void>::success();
}

core::Result<void> UiDocument::refreshBindings(const scene::Scene& sourceScene)
{
    struct PendingUpdate { UiElement* element; std::string text; bool visible; };
    std::vector<PendingUpdate> pending;
    pending.reserve(elements_.size());
    for (auto& element : elements_)
    {
        if (!element.binding) continue;
        const auto valid = validateBinding(element, *element.binding, sourceScene);
        if (!valid) return valid;
        const auto& binding = *element.binding;
        const auto* entity = sourceScene.find(binding.entity);
        if (binding.source == BindingSource::EntityName)
            pending.push_back({&element, entity->name, element.visible});
        else
            pending.push_back({&element, element.text, sourceScene.spriteRenderer(binding.entity)->visible});
    }
    for (auto& update : pending)
    {
        if (update.element->binding->target == BindingTarget::Text) update.element->text = std::move(update.text);
        else update.element->visible = update.visible;
    }
    return core::Result<void>::success();
}

std::string_view elementKindName(const ElementKind kind) noexcept
{
    switch (kind)
    {
    case ElementKind::Panel: return "Panel";
    case ElementKind::Text: return "Text";
    case ElementKind::Button: return "Button";
    }
    return "Unknown";
}

std::string_view bindingSourceName(const BindingSource source) noexcept
{
    switch (source)
    {
    case BindingSource::EntityName: return "Entity.Name";
    case BindingSource::SpriteVisible: return "SpriteRenderer.Visible";
    }
    return "Unknown";
}

std::string_view bindingTargetName(const BindingTarget target) noexcept
{
    switch (target)
    {
    case BindingTarget::Text: return "Text";
    case BindingTarget::Visible: return "Visible";
    }
    return "Unknown";
}
}

# UI Editor — 06.2

Open the editor and choose **View → UI Editor**. The panel lets you add Panel, Text and Button elements, edit Text content, remove elements, change the preview theme, and select Desktop (1280×720), Tablet (800×1000) or Phone (390×844) preview sizes.

Select an element to edit its normalized minimum/maximum anchors and pixel offsets. Anchors resolve against the parent element's rectangle; a root element resolves against the viewport. The preview redraws the layout at the selected viewport size. Dark and Light are built-in theme presets for the preview.

## Scene bindings

For a selected element, choose a scene entity and one supported binding:

- `Entity.Name → Text` updates a Text or Button element's content.
- `SpriteRenderer.Visible → Visible` updates an element's visibility.

The editor refreshes these bindings from the current Scene. The binding evaluator only reads Scene and changes the UI document. Missing entities and incompatible element/property pairs produce diagnostics; one failed binding prevents a refresh from partially applying values.

## C++ model example

```cpp
void setupUiPreview()
{
    astro::ui::UiDocument document;
    const auto title = document.addElement(astro::ui::ElementKind::Text, "Player name");
    if (!title) return;

    astro::ui::ResponsiveRect layout;
    layout.anchorMinX = 0.05;
    layout.anchorMinY = 0.05;
    layout.anchorMaxX = 0.95;
    layout.anchorMaxY = 0.15;
    layout.offsetMaxX = -8.0;
    layout.offsetMaxY = -8.0;
    if (!document.setLayout(title.value(), layout)) return;
    const auto bounds = document.resolveLayout(title.value(), 390.0, 844.0);
    if (!bounds) return;
}
```

Callers should check each `Result`; the example keeps the setup compact. Unit and integration coverage lives in `tests/unit/ui/UiDocumentTests.cpp` and `tests/integration/ui/UiBindingIntegrationTests.cpp`.

## Current limits

- Documents and theme selection are in editor memory only. There is no UI-file serialization, save/load, migration or Undo history.
- There is no connection to the Game UI renderer or exported runtime. Preview is an editor canvas.
- Binding sources are limited to `Entity.Name` and `SpriteRenderer.Visible`; there are no health/inventory/model properties, event actions or two-way bindings.
- UI panel authoring creates root elements only. Parent-aware responsive resolution is available through the C++ model, but hierarchy reparenting is not exposed in the panel.
- No keyboard/gamepad navigation, localization, custom theme editor, asset/image widget or pixel-golden screenshot has been added.
- Element count is bounded at 512, text at 4096 UTF-8 bytes, element names at 120 bytes, offsets at one million pixels, and preview viewports at 65536×65536.

The editor target uses the existing SDL3/ImGui stack; no new third-party dependency was added. This substage is an authoring prototype and does not meet the full Stage 06 criterion of assembling and running a new game mechanic without C++.

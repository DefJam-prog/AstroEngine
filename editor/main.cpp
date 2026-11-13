#include <filesystem>
#include <fstream>
#include <string>
#include "ProjectFile.hpp"
#include "astro/assets/AssetRegistry.hpp"
#include "astro/logic/VisualLogicGraph.hpp"
#include "astro/runtime/AnimationTimeline.hpp"
#include "astro/runtime/CameraRig.hpp"
#include "astro/scene/Scene.hpp"
#include "astro/scene/SceneFile.hpp"
#include "astro/ui/UiDocument.hpp"
#include <SDL3/SDL.h>
#include <bgfx/bgfx.h>
#include <bx/math.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_sdl3.h>
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <vector>
#include <iterator>
#include <map>
#include <memory>
#include <limits>

// Upstream bgfx shader binaries; licenses remain in the dependency cache.
#include <vs_ocornut_imgui.bin.h>
#include <fs_ocornut_imgui.bin.h>

namespace
{
using astro::scene::Entity;
using astro::scene::EntityId;
using astro::scene::Scene;
using Microsoft::WRL::ComPtr;

void appendPngBe32(std::vector<std::uint8_t>& output, const std::uint32_t value)
{
    output.push_back(static_cast<std::uint8_t>(value >> 24));
    output.push_back(static_cast<std::uint8_t>(value >> 16));
    output.push_back(static_cast<std::uint8_t>(value >> 8));
    output.push_back(static_cast<std::uint8_t>(value));
}

std::uint32_t pngCrc32(const std::uint8_t* bytes, const std::size_t size)
{
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < size; ++i)
    {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

void appendPngChunk(std::vector<std::uint8_t>& png, const std::array<char, 4> type,
    const std::vector<std::uint8_t>& data)
{
    appendPngBe32(png, static_cast<std::uint32_t>(data.size()));
    const auto start = png.size();
    for (const auto c : type) png.push_back(static_cast<std::uint8_t>(c));
    png.insert(png.end(), data.begin(), data.end());
    appendPngBe32(png, pngCrc32(png.data() + start, png.size() - start));
}

std::vector<std::uint8_t> makeSmokePng()
{
    std::vector<std::uint8_t> png{137,80,78,71,13,10,26,10};
    std::vector<std::uint8_t> header;
    appendPngBe32(header, 1); appendPngBe32(header, 1);
    header.insert(header.end(), {8, 6, 0, 0, 0});
    appendPngChunk(png, {'I','H','D','R'}, header);
    const std::vector<std::uint8_t> compressed{0x78,0x01,0x01,0x05,0x00,0xFA,0xFF,0x00,0xFF,0x00,0x00,0xFF,0x05,0x00,0x01,0xFF};
    appendPngChunk(png, {'I','D','A','T'}, compressed);
    appendPngChunk(png, {'I','E','N','D'}, {});
    return png;
}

bool hasChildren(const Scene& scene, const EntityId id)
{
    return std::any_of(scene.entities().begin(), scene.entities().end(), [id](const Entity& entity) {
        return entity.parent == id;
    });
}

void drawHierarchyBranch(const Scene& scene, const std::optional<EntityId> parent,
    std::optional<EntityId>& selected)
{
    for (const auto& entity : scene.entities())
    {
        if (entity.parent != parent) continue;
        const bool children = hasChildren(scene, entity.id);
        const std::string label = entity.name + "##" + std::to_string(entity.id);
        const auto flags = (children ? ImGuiTreeNodeFlags_OpenOnArrow : ImGuiTreeNodeFlags_Leaf)
            | ImGuiTreeNodeFlags_SpanAvailWidth
            | (selected == entity.id ? ImGuiTreeNodeFlags_Selected : 0);
        if (ImGui::TreeNodeEx(label.c_str(), flags))
        {
            if (ImGui::IsItemClicked()) selected = entity.id;
            if (children) drawHierarchyBranch(scene, entity.id, selected);
            if (children) ImGui::TreePop();
        }
        else if (ImGui::IsItemClicked()) selected = entity.id;
    }
}

astro::logic::VisualLogicGraph makeScoreGateGraph()
{
    using astro::logic::NodeKind;
    astro::logic::VisualLogicGraph graph("Score gate example");
    const auto score = graph.addNode(NodeKind::NumberConstant);
    const auto bonus = graph.addNode(NodeKind::NumberConstant);
    const auto total = graph.addNode(NodeKind::Add);
    const auto threshold = graph.addNode(NodeKind::NumberConstant);
    const auto passed = graph.addNode(NodeKind::GreaterThan);
    const auto enabled = graph.addNode(NodeKind::BooleanConstant);
    const auto result = graph.addNode(NodeKind::And);
    if (!score || !bonus || !total || !threshold || !passed || !enabled || !result)
        throw std::runtime_error("Could not create Visual Logic example nodes");
    if (!graph.setLiteral(score.value(), 4.0) || !graph.setLiteral(bonus.value(), 3.0)
        || !graph.setLiteral(threshold.value(), 6.0) || !graph.setLiteral(enabled.value(), true)
        || !graph.connect(score.value(), total.value(), 0) || !graph.connect(bonus.value(), total.value(), 1)
        || !graph.connect(total.value(), passed.value(), 0) || !graph.connect(threshold.value(), passed.value(), 1)
        || !graph.connect(passed.value(), result.value(), 0) || !graph.connect(enabled.value(), result.value(), 1))
        throw std::runtime_error("Could not connect Visual Logic example graph");
    return graph;
}

void reportLogicResult(const astro::core::Result<void>& result,
    const std::function<void(const std::string&)>& reportError)
{
    if (!result) reportError(result.error().message);
}

void drawVisualLogic(astro::logic::VisualLogicGraph& graph, astro::logic::VisualLogicDebugger& debugger,
    const std::function<void(const std::string&)>& reportError, bool& windowDrawn)
{
    using namespace astro::logic;
    if (!ImGui::Begin("Visual Logic")) { ImGui::End(); return; }
    windowDrawn = true;
    ImGui::Text("%s", graph.name().c_str());
    ImGui::TextDisabled("Typed dataflow prototype. Inputs connect through typed menus; graph is not saved yet.");

    const auto add = [&](const NodeKind kind) {
        const auto result = graph.addNode(kind);
        if (!result) reportError(result.error().message);
        else debugger.reset();
    };
    const auto addButton = [&](const char* label, const NodeKind kind) {
        ImGui::PushID(static_cast<int>(kind));
        if (ImGui::Button(label)) add(kind);
        ImGui::PopID();
    };
    addButton("+ Number", NodeKind::NumberConstant);
    ImGui::SameLine(); addButton("+ Boolean", NodeKind::BooleanConstant);
    ImGui::SameLine(); addButton("+ String", NodeKind::StringConstant);
    ImGui::SameLine();
    if (ImGui::BeginMenu("+ Math"))
    {
        if (ImGui::MenuItem("Add")) add(NodeKind::Add);
        if (ImGui::MenuItem("Subtract")) add(NodeKind::Subtract);
        if (ImGui::MenuItem("Multiply")) add(NodeKind::Multiply);
        if (ImGui::MenuItem("Divide")) add(NodeKind::Divide);
        ImGui::EndMenu();
    }
    ImGui::SameLine();
    if (ImGui::BeginMenu("+ Compare"))
    {
        if (ImGui::MenuItem("Greater than")) add(NodeKind::GreaterThan);
        if (ImGui::MenuItem("Less than")) add(NodeKind::LessThan);
        if (ImGui::MenuItem("Equal number")) add(NodeKind::EqualNumber);
        ImGui::EndMenu();
    }
    ImGui::SameLine();
    if (ImGui::BeginMenu("+ Logic"))
    {
        if (ImGui::MenuItem("And")) add(NodeKind::And);
        if (ImGui::MenuItem("Or")) add(NodeKind::Or);
        if (ImGui::MenuItem("Not")) add(NodeKind::Not);
        ImGui::EndMenu();
    }
    ImGui::SameLine();
    if (ImGui::BeginMenu("+ Text"))
    {
        if (ImGui::MenuItem("Concatenate")) add(NodeKind::Concatenate);
        ImGui::EndMenu();
    }

    if (ImGui::Button("Run"))
    {
        auto started = debugger.start(graph);
        if (!started) reportError(started.error().message);
        else reportLogicResult(debugger.runToEnd(), reportError);
    }
    ImGui::SameLine();
    if (ImGui::Button("Step"))
    {
        if (debugger.state() != DebugState::Running)
        {
            const auto started = debugger.start(graph);
            if (!started) reportError(started.error().message);
        }
        if (debugger.state() == DebugState::Running)
        {
            const auto stepped = debugger.step();
            if (!stepped) reportError(stepped.error().message);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset debug")) debugger.reset();
    ImGui::SameLine();
    ImGui::TextDisabled("Debugger: %s", debugger.state() == DebugState::Idle ? "idle"
        : debugger.state() == DebugState::Running ? "running"
        : debugger.state() == DebugState::Completed ? "complete" : "faulted");

    const auto available = ImGui::GetContentRegionAvail();
    ImGui::BeginChild("LogicNodes", ImVec2(0, available.y), true);
    for (const auto& node : graph.nodes())
    {
        ImGui::PushID(static_cast<int>(node.id));
        ImGui::Separator();
        ImGui::Text("Node %u — %s", node.id, nodeKindName(node.kind));
        ImGui::SameLine();
        ImGui::TextDisabled("Output: %s", valueTypeName(outputType(node.kind)));

        if (node.kind == NodeKind::NumberConstant)
        {
            auto value = std::get<double>(node.literal);
            if (ImGui::InputDouble("Value", &value))
            {
                const auto changed = graph.setLiteral(node.id, value);
                if (!changed) reportError(changed.error().message);
                else debugger.reset();
            }
        }
        else if (node.kind == NodeKind::BooleanConstant)
        {
            auto value = std::get<bool>(node.literal);
            if (ImGui::Checkbox("Value", &value))
            {
                const auto changed = graph.setLiteral(node.id, value);
                if (!changed) reportError(changed.error().message);
                else debugger.reset();
            }
        }
        else if (node.kind == NodeKind::StringConstant)
        {
            std::array<char, 256> text{};
            const auto& value = std::get<std::string>(node.literal);
            std::copy_n(value.data(), std::min(value.size(), text.size() - 1), text.data());
            if (ImGui::InputText("Value", text.data(), text.size()))
            {
                const auto changed = graph.setLiteral(node.id, std::string(text.data()));
                if (!changed) reportError(changed.error().message);
                else debugger.reset();
            }
        }

        for (std::size_t input = 0; input < node.inputs.size(); ++input)
        {
            const auto expected = inputType(node.kind, input);
            if (!expected) continue;
            std::string selected = "<unconnected>";
            if (node.inputs[input])
            {
                if (const auto* source = graph.find(*node.inputs[input]))
                    selected = "Node " + std::to_string(source->id) + " — " + nodeKindName(source->kind);
            }
            const auto label = "Input " + std::to_string(input + 1) + " (" + valueTypeName(*expected) + ")";
            if (ImGui::BeginCombo(label.c_str(), selected.c_str()))
            {
                if (ImGui::Selectable("<unconnected>"))
                {
                    const auto changed = graph.disconnect(node.id, input);
                    if (!changed) reportError(changed.error().message);
                    else debugger.reset();
                }
                for (const auto& candidate : graph.nodes())
                {
                    if (candidate.id == node.id || outputType(candidate.kind) != *expected) continue;
                    const auto candidateLabel = "Node " + std::to_string(candidate.id) + " — " + nodeKindName(candidate.kind);
                    if (ImGui::Selectable(candidateLabel.c_str(), node.inputs[input] == candidate.id))
                    {
                        const auto disconnected = graph.disconnect(node.id, input);
                        if (!disconnected) reportError(disconnected.error().message);
                        else
                        {
                            debugger.reset();
                            const auto changed = graph.connect(candidate.id, node.id, input);
                            if (!changed) reportError(changed.error().message);
                        }
                    }
                }
                ImGui::EndCombo();
            }
        }

        if (const auto* state = debugger.trace(node.id))
        {
            if (state->state == NodeState::Succeeded && state->output)
                ImGui::TextColored(ImVec4(0.35f, 0.9f, 0.45f, 1.0f), "Output: %s", formatValue(*state->output).c_str());
            else if (state->state == NodeState::Failed)
                ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.25f, 1.0f), "Error: %s", state->diagnostic.c_str());
            else ImGui::TextDisabled("Debug state: pending");
        }
        if (ImGui::SmallButton("Remove node"))
        {
            const auto removed = graph.removeNode(node.id);
            if (!removed) reportError(removed.error().message);
            else debugger.reset();
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::End();
}

ImU32 uiColor(const std::uint32_t rgba)
{
    return IM_COL32((rgba >> 24) & 0xFF, (rgba >> 16) & 0xFF, (rgba >> 8) & 0xFF, rgba & 0xFF);
}

void drawUiEditor(astro::ui::UiDocument& document, const Scene& scene,
    std::optional<astro::ui::ElementId>& selected, int& viewportPreset,
    std::array<char, 4097>& textDraft, std::optional<astro::ui::ElementId>& draftElement,
    const std::function<void(const std::string&)>& reportError, bool& windowDrawn)
{
    using namespace astro::ui;
    if (!ImGui::Begin("UI Editor")) { ImGui::End(); return; }
    windowDrawn = true;

    static constexpr const char* themeNames[]{"Dark", "Light"};
    int themeIndex = document.theme() == ThemeKind::Light ? 1 : 0;
    if (ImGui::Combo("Theme", &themeIndex, themeNames, 2))
    {
        const auto changed = document.setTheme(themeIndex == 0 ? ThemeKind::Dark : ThemeKind::Light);
        if (!changed) reportError(changed.error().message);
    }
    static constexpr const char* viewportNames[]{"Desktop 1280 x 720", "Tablet 800 x 1000", "Phone 390 x 844"};
    ImGui::Combo("Preview size", &viewportPreset, viewportNames, 3);

    const auto create = [&](const ElementKind kind, const char* name) {
        const auto added = document.addElement(kind, name);
        if (!added) reportError(added.error().message);
        else { selected = added.value(); draftElement.reset(); }
    };
    if (ImGui::Button("+ Panel")) create(ElementKind::Panel, "Panel");
    ImGui::SameLine();
    if (ImGui::Button("+ Text")) create(ElementKind::Text, "Text");
    ImGui::SameLine();
    if (ImGui::Button("+ Button")) create(ElementKind::Button, "Button");
    ImGui::SameLine();
    ImGui::TextDisabled("UI layout is an in-memory editor document in 06.2.");

    const auto body = ImGui::GetContentRegionAvail();
    const float listWidth = std::clamp(body.x * 0.22f, 150.0f, 230.0f);
    if (ImGui::BeginChild("UiElementList", ImVec2(listWidth, body.y), true))
    {
        for (const auto& element : document.elements())
        {
            if (ImGui::Selectable((element.name + "  [" + std::string(elementKindName(element.kind)) + "]##ui"
                + std::to_string(element.id)).c_str(), selected == element.id))
            {
                selected = element.id;
                draftElement.reset();
            }
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();

    const float inspectorWidth = std::clamp(body.x * 0.30f, 230.0f, 330.0f);
    if (ImGui::BeginChild("UiInspector", ImVec2(inspectorWidth, body.y), true))
    {
        const auto* element = selected ? document.find(*selected) : nullptr;
        if (!element) ImGui::TextDisabled("Create or select an element.");
        else
        {
            ImGui::Text("%s — %s", element->name.c_str(), elementKindName(element->kind).data());
            if (ImGui::Button("Remove selected element"))
            {
                const auto removed = document.removeElement(element->id);
                if (!removed) reportError(removed.error().message);
                else { selected.reset(); draftElement.reset(); element = nullptr; }
            }
            if (!element)
            {
                ImGui::TextDisabled("Element removed.");
            }
            else
            {
                ImGui::SeparatorText("Content");
                if (element->kind != ElementKind::Panel)
                {
                    if (draftElement != selected)
                    {
                        textDraft.fill('\0');
                        std::copy_n(element->text.data(), std::min(element->text.size(), textDraft.size() - 1), textDraft.data());
                        draftElement = selected;
                    }
                    const bool boundText = element->binding && element->binding->target == BindingTarget::Text;
                    ImGui::BeginDisabled(boundText);
                    if (ImGui::InputTextMultiline("Text", textDraft.data(), textDraft.size(), ImVec2(-1.0f, 68.0f)))
                    {
                        const auto changed = document.setText(element->id, textDraft.data());
                        if (!changed) reportError(changed.error().message);
                    }
                    ImGui::EndDisabled();
                    if (boundText) ImGui::TextDisabled("Text is driven by its scene binding.");
                }

                ImGui::SeparatorText("Responsive layout");
                ResponsiveRect layout = element->layout;
                bool layoutChanged = false;
                ImGui::TextDisabled("Anchors are normalized to the parent area; offsets are pixels.");
                layoutChanged |= ImGui::InputDouble("Anchor min X", &layout.anchorMinX, 0.01, 0.1, "%.2f");
                layoutChanged |= ImGui::InputDouble("Anchor min Y", &layout.anchorMinY, 0.01, 0.1, "%.2f");
                layoutChanged |= ImGui::InputDouble("Anchor max X", &layout.anchorMaxX, 0.01, 0.1, "%.2f");
                layoutChanged |= ImGui::InputDouble("Anchor max Y", &layout.anchorMaxY, 0.01, 0.1, "%.2f");
                layoutChanged |= ImGui::InputDouble("Offset min X", &layout.offsetMinX, 1.0, 10.0, "%.0f");
                layoutChanged |= ImGui::InputDouble("Offset min Y", &layout.offsetMinY, 1.0, 10.0, "%.0f");
                layoutChanged |= ImGui::InputDouble("Offset max X", &layout.offsetMaxX, 1.0, 10.0, "%.0f");
                layoutChanged |= ImGui::InputDouble("Offset max Y", &layout.offsetMaxY, 1.0, 10.0, "%.0f");
                if (layoutChanged)
                {
                    const auto changed = document.setLayout(element->id, layout);
                    if (!changed) reportError(changed.error().message);
                }

                ImGui::SeparatorText("Scene binding");
                std::vector<const char*> entityLabels;
                entityLabels.reserve(scene.entities().size());
                int entityIndex = 0;
                for (std::size_t i = 0; i < scene.entities().size(); ++i)
                {
                    entityLabels.push_back(scene.entities()[i].name.c_str());
                    if (element->binding && scene.entities()[i].id == element->binding->entity)
                        entityIndex = static_cast<int>(i);
                }
                if (entityLabels.empty()) ImGui::TextDisabled("Create a scene entity to bind values.");
                else
                {
                    ImGui::Combo("Entity", &entityIndex, entityLabels.data(), static_cast<int>(entityLabels.size()));
                    static constexpr const char* textBindingNames[]{"No binding", "Entity.Name → Text"};
                    static constexpr const char* visibleBindingNames[]{"No binding", "SpriteRenderer.Visible → Visible"};
                    const bool textAllowed = element->kind == ElementKind::Text || element->kind == ElementKind::Button;
                    const auto* boundEntity = scene.find(scene.entities()[static_cast<std::size_t>(entityIndex)].id);
                    const bool visibleAllowed = boundEntity && scene.spriteRenderer(boundEntity->id);
                    int bindingIndex = 0;
                    if (element->binding && element->binding->entity == scene.entities()[static_cast<std::size_t>(entityIndex)].id)
                    {
                        if (element->binding->source == BindingSource::EntityName) bindingIndex = 1;
                        else if (element->binding->source == BindingSource::SpriteVisible) bindingIndex = 1;
                    }
                    const char* const* labels = textAllowed ? textBindingNames : visibleBindingNames;
                    if (textAllowed || visibleAllowed)
                    {
                        if (ImGui::Combo("Bind property", &bindingIndex, labels, 2))
                        {
                            std::optional<UiBinding> binding;
                            if (bindingIndex == 1)
                                binding = textAllowed
                                    ? UiBinding{boundEntity->id, BindingSource::EntityName, BindingTarget::Text}
                                    : UiBinding{boundEntity->id, BindingSource::SpriteVisible, BindingTarget::Visible};
                            const auto changed = document.setBinding(element->id, binding, scene);
                            if (!changed) reportError(changed.error().message);
                        }
                    }
                    else ImGui::TextDisabled("This element has no supported binding target for this entity.");
                }
                if (element->binding && ImGui::Button("Clear binding"))
                {
                    const auto cleared = document.setBinding(element->id, std::nullopt, scene);
                    if (!cleared) reportError(cleared.error().message);
                }
            }
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();

    if (ImGui::BeginChild("UiPreview", ImVec2(0, body.y), true))
    {
        const double viewportWidth = viewportPreset == 0 ? 1280.0 : viewportPreset == 1 ? 800.0 : 390.0;
        const double viewportHeight = viewportPreset == 0 ? 720.0 : viewportPreset == 1 ? 1000.0 : 844.0;
        ImGui::Text("Preview — %.0f x %.0f", viewportWidth, viewportHeight);
        ImGui::TextDisabled("Change preview size to check anchors and offsets.");
        const auto available = ImGui::GetContentRegionAvail();
        const double scale = std::min(static_cast<double>(available.x) / viewportWidth,
            static_cast<double>(available.y) / viewportHeight);
        const double canvasWidth = viewportWidth * std::max(0.01, scale);
        const double canvasHeight = viewportHeight * std::max(0.01, scale);
        const auto origin = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("UiPreviewCanvas", ImVec2(static_cast<float>(canvasWidth), static_cast<float>(canvasHeight)));
        auto* draw = ImGui::GetWindowDrawList();
        const auto theme = document.resolvedTheme();
        const ImVec2 canvasMax(origin.x + static_cast<float>(canvasWidth), origin.y + static_cast<float>(canvasHeight));
        draw->AddRectFilled(origin, canvasMax, uiColor(theme.background));
        draw->PushClipRect(origin, canvasMax, true);
        for (const auto& item : document.elements())
        {
            if (!item.visible) continue;
            const auto resolved = document.resolveLayout(item.id, viewportWidth, viewportHeight);
            if (!resolved) { reportError(resolved.error().message); continue; }
            const auto& rect = resolved.value();
            const ImVec2 min(origin.x + static_cast<float>(rect.x * scale), origin.y + static_cast<float>(rect.y * scale));
            const ImVec2 max(min.x + static_cast<float>(rect.width * scale), min.y + static_cast<float>(rect.height * scale));
            if (item.kind == ElementKind::Panel) draw->AddRectFilled(min, max, uiColor(theme.surface), 3.0f);
            else if (item.kind == ElementKind::Button) draw->AddRectFilled(min, max, uiColor(theme.accent), 4.0f);
            const auto& caption = item.text.empty() ? item.name : item.text;
            draw->AddText(ImVec2(min.x + 5.0f, min.y + 5.0f), uiColor(theme.text), caption.c_str());
            if (selected == item.id) draw->AddRect(min, max, IM_COL32(255, 205, 90, 255), 2.0f, 0, 2.0f);
        }
        draw->PopClipRect();
    }
    ImGui::EndChild();
    ImGui::End();
}

void drawTimelineEditor(astro::runtime::AnimationTimeline& timeline, const float deltaSeconds,
    const std::function<void(const std::string&)>& reportError, bool& windowDrawn)
{
    if (!ImGui::Begin("Timeline")) { ImGui::End(); return; }
    windowDrawn = true;
    if (timeline.isPlaying())
    {
        const auto advanced = timeline.advance(deltaSeconds);
        if (!advanced) reportError(advanced.error().message);
    }
    if (ImGui::Button(timeline.isPlaying() ? "Pause" : "Play"))
    {
        if (timeline.isPlaying()) timeline.pause();
        else timeline.play();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset")) timeline.reset();
    ImGui::SameLine();
    if (ImGui::Button("Add key at end"))
    {
        const auto added = timeline.addKeyframe({timeline.endTime() + 1.0, timeline.sample()});
        if (!added) reportError(added.error().message);
    }
    ImGui::Text("Scalar track — %.3f", timeline.sample());
    double playhead = timeline.playhead();
    const double start = timeline.startTime(), end = timeline.endTime();
    if (start < end && ImGui::SliderScalar("Playhead", ImGuiDataType_Double, &playhead, &start, &end, "%.2f s"))
    {
        const auto sought = timeline.seek(playhead);
        if (!sought) reportError(sought.error().message);
    }

    const auto graphSize = ImVec2(-1.0f, 120.0f);
    if (ImGui::BeginChild("TimelineCurve", graphSize, true))
    {
        const auto origin = ImGui::GetCursorScreenPos();
        const auto available = ImGui::GetContentRegionAvail();
        const ImVec2 max(origin.x + available.x, origin.y + available.y);
        auto* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(origin, max, IM_COL32(24, 29, 38, 255));
        double minimum = timeline.keyframes().front().value;
        double maximum = minimum;
        for (const auto& key : timeline.keyframes())
        {
            minimum = std::min(minimum, key.value);
            maximum = std::max(maximum, key.value);
        }
        if (minimum == maximum) { minimum -= 1.0; maximum += 1.0; }
        const double timeRange = end - start;
        const auto point = [&](const double time) {
            const double x = (time - start) / timeRange;
            const double y = (timeline.sampleAt(time) - minimum) / (maximum - minimum);
            return ImVec2(origin.x + static_cast<float>(x * available.x),
                max.y - static_cast<float>(y * available.y));
        };
        ImVec2 previous = point(start);
        for (int sample = 1; sample <= 64; ++sample)
        {
            const double time = start + timeRange * static_cast<double>(sample) / 64.0;
            const ImVec2 current = point(time);
            draw->AddLine(previous, current, IM_COL32(100, 175, 255, 255), 2.0f);
            previous = current;
        }
        for (const auto& key : timeline.keyframes())
        {
            const double x = (key.timeSeconds - start) / timeRange;
            const double y = (key.value - minimum) / (maximum - minimum);
            draw->AddCircleFilled(ImVec2(origin.x + static_cast<float>(x * available.x),
                max.y - static_cast<float>(y * available.y)), 4.0f, IM_COL32(255, 205, 90, 255));
        }
        const double headX = (timeline.playhead() - start) / timeRange;
        const float x = origin.x + static_cast<float>(headX * available.x);
        draw->AddLine(ImVec2(x, origin.y), ImVec2(x, max.y), IM_COL32(255, 90, 90, 220), 1.0f);
    }
    ImGui::EndChild();

    ImGui::SeparatorText("Keyframes");
    bool removed = false;
    for (std::size_t index = 0; index < timeline.keyframes().size(); ++index)
    {
        ImGui::PushID(static_cast<int>(index));
        double keyTime = timeline.keyframes()[index].timeSeconds;
        double value = timeline.keyframes()[index].value;
        bool changed = ImGui::InputDouble("Time", &keyTime, 0.01, 0.1, "%.3f");
        ImGui::SameLine();
        changed |= ImGui::InputDouble("Value", &value, 0.1, 1.0, "%.3f");
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove"))
        {
            const auto result = timeline.removeKeyframe(index);
            if (!result) reportError(result.error().message);
            removed = true;
        }
        else if (changed)
        {
            const auto result = timeline.updateKeyframe(index, {keyTime, value});
            if (!result) reportError(result.error().message);
        }
        ImGui::PopID();
        if (removed) break;
    }
    ImGui::End();
}

void drawCameraPresets(astro::runtime::CameraRig& camera, const Scene& scene,
    const std::optional<EntityId> selectedEntity, const float deltaSeconds,
    const std::function<void(const std::string&)>& reportError, bool& windowDrawn)
{
    using astro::runtime::CameraMode;
    if (!ImGui::Begin("Camera Presets")) { ImGui::End(); return; }
    windowDrawn = true;
    static constexpr const char* modes[]{"Custom", "First Person", "Third Person", "Isometric", "Top Down", "Platformer", "Strategy"};
    int mode = static_cast<int>(camera.settings().mode);
    if (ImGui::Combo("Preset", &mode, modes, 7))
    {
        const auto changed = camera.setMode(static_cast<CameraMode>(mode));
        if (!changed) reportError(changed.error().message);
    }
    std::vector<const char*> entityNames;
    entityNames.reserve(scene.entities().size());
    int entityIndex = 0;
    for (std::size_t index = 0; index < scene.entities().size(); ++index)
    {
        entityNames.push_back(scene.entities()[index].name.c_str());
        if (camera.settings().target == scene.entities()[index].id) entityIndex = static_cast<int>(index);
    }
    if (!entityNames.empty())
    {
        if (ImGui::Combo("Follow entity", &entityIndex, entityNames.data(), static_cast<int>(entityNames.size())))
        {
            auto settings = camera.settings();
            settings.target = scene.entities()[static_cast<std::size_t>(entityIndex)].id;
            const auto configured = camera.configure(settings);
            if (!configured) reportError(configured.error().message);
            else if (camera.settings().mode != CameraMode::Custom)
                reportLogicResult(camera.snapToTarget(scene), reportError);
        }
    }
    else ImGui::TextDisabled("Create a scene entity to use a follow preset.");
    if (selectedEntity && scene.find(*selectedEntity) && ImGui::Button("Use selected Hierarchy entity"))
    {
        auto settings = camera.settings();
        settings.target = selectedEntity;
        const auto configured = camera.configure(settings);
        if (!configured) reportError(configured.error().message);
        else if (camera.settings().mode != CameraMode::Custom)
            reportLogicResult(camera.snapToTarget(scene), reportError);
    }
    if (camera.settings().mode != CameraMode::Custom && camera.settings().target
        && scene.find(*camera.settings().target))
    {
        const auto updated = camera.update(scene, deltaSeconds);
        if (!updated) reportError(updated.error().message);
    }
    else if (camera.settings().mode != CameraMode::Custom)
        ImGui::TextDisabled("Select a valid target entity for this follow preset.");
    if (camera.settings().mode == CameraMode::Custom)
    {
        auto pose = camera.pose();
        bool changed = false;
        ImGui::SeparatorText("Custom pose");
        changed |= ImGui::InputDouble("Position X", &pose.position.x, 0.1, 1.0, "%.3f");
        changed |= ImGui::InputDouble("Position Y", &pose.position.y, 0.1, 1.0, "%.3f");
        changed |= ImGui::InputDouble("Position Z", &pose.position.z, 0.1, 1.0, "%.3f");
        changed |= ImGui::InputDouble("Rotation X", &pose.rotationDegrees.x, 1.0, 10.0, "%.2f");
        changed |= ImGui::InputDouble("Rotation Y", &pose.rotationDegrees.y, 1.0, 10.0, "%.2f");
        changed |= ImGui::InputDouble("Rotation Z", &pose.rotationDegrees.z, 1.0, 10.0, "%.2f");
        changed |= ImGui::InputDouble("Zoom", &pose.zoom, 0.1, 1.0, "%.2f");
        if (changed)
        {
            const auto updated = camera.setPose(pose);
            if (!updated) reportError(updated.error().message);
        }
    }
    const auto& pose = camera.pose();
    ImGui::SeparatorText("Computed pose");
    ImGui::Text("Position: %.2f, %.2f, %.2f", pose.position.x, pose.position.y, pose.position.z);
    ImGui::Text("Rotation: %.2f, %.2f, %.2f", pose.rotationDegrees.x, pose.rotationDegrees.y, pose.rotationDegrees.z);
    ImGui::Text("Zoom: %.2f", pose.zoom);
    ImGui::TextDisabled("Pose preview only; camera image rendering is not connected.");
    ImGui::End();
}

struct SceneNodeLayout
{
    const Entity* entity;
    int depth;
    int row;
};

void drawInspector(Scene& scene, std::optional<EntityId>& selected, astro::core::CommandRegistry& commands,
    std::array<char, 256>& textDraft, std::optional<EntityId>& draftEntity,
    std::optional<astro::scene::SceneId>& draftScene, bool& sceneDirty, std::string& message,
    const astro::assets::AssetRegistry* assets, const std::map<std::uint64_t, bgfx::TextureHandle>& textures,
    const std::function<void(const std::string&)>& reportError)
{
    if (!selected || !scene.find(*selected))
    {
        draftEntity.reset();
        draftScene.reset();
        ImGui::TextDisabled("Select an entity in Hierarchy or Scene View.");
        return;
    }

    const auto entityId = *selected;
    ImGui::Text("Entity %llu", static_cast<unsigned long long>(entityId));
    ImGui::Separator();
    for (const auto& property : astro::scene::entityPropertySchema())
    {
        const auto value = scene.getProperty(entityId, property.id);
        if (!value)
        {
            reportError(value.error().message);
            continue;
        }
        const std::string label(property.label);
        if (property.type == astro::scene::PropertyType::UnsignedInteger)
        {
            const auto* number = std::get_if<EntityId>(&value.value());
            if (number)
                ImGui::Text("%s: %llu", label.c_str(), static_cast<unsigned long long>(*number));
            continue;
        }

        const auto* text = std::get_if<std::string>(&value.value());
        if (!text) continue;
        if (draftEntity != entityId || draftScene != scene.id())
        {
            const auto count = std::min(text->size(), textDraft.size() - 1);
            std::copy_n(text->data(), count, textDraft.data());
            textDraft[count] = '\0';
            draftEntity = entityId;
            draftScene = scene.id();
        }
        ImGui::InputText(label.c_str(), textDraft.data(), textDraft.size());
        const std::string draft(textDraft.data());
        const bool changed = draft != *text;
        ImGui::BeginDisabled(!changed);
        if (ImGui::Button(("Apply " + label).c_str()))
        {
            astro::scene::SetScenePropertyPayload payload{entityId, std::string(property.id), draft};
            auto applied = commands.execute<astro::scene::SetScenePropertyPayload, void>(
                "entity.set_property", 1, payload);
            if (applied) { sceneDirty = true; message = "Property applied through entity.set_property"; }
            else reportError(applied.error().message);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("UTF-8, max %zu bytes", property.maxLengthBytes);
    }
    ImGui::Separator();
    ImGui::TextUnformatted("SpriteRenderer");
    if (!assets || assets->entries().empty()) ImGui::TextDisabled("Import a PNG or JPEG in Project first.");
    else
    {
        const auto* sprite = scene.spriteRenderer(entityId);
        std::uint64_t selectedAsset = sprite ? sprite->texture.value : assets->entries().begin()->first.value;
        if (ImGui::BeginCombo("Texture Asset", std::to_string(selectedAsset).c_str()))
        {
            for (const auto& [id, reference] : assets->entries())
            {
                const bool isSelected = selectedAsset == id.value;
                const auto label = std::to_string(id.value) + " — " + reference.sourceRelativePath;
                if (ImGui::Selectable(label.c_str(), isSelected)) selectedAsset = id.value;
                if (isSelected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        const bool hasMatchingAsset = assets->find({selectedAsset}) != nullptr;
        const bool changed = !sprite || sprite->texture.value != selectedAsset;
        ImGui::BeginDisabled(!changed || !hasMatchingAsset);
        if (ImGui::Button(sprite ? "Assign texture" : "Add SpriteRenderer"))
        {
            const astro::scene::SetSpriteRendererPayload payload{entityId, {selectedAsset}, true};
            auto result = commands.execute<astro::scene::SetSpriteRendererPayload, void>(
                "entity.set_sprite_renderer", 1, payload);
            if (result) { sceneDirty = true; message = "SpriteRenderer assigned through command API"; }
            else reportError(result.error().message);
        }
        ImGui::EndDisabled();
        if (sprite)
        {
            const auto asset = assets->find(sprite->texture);
            if (!asset) ImGui::TextColored(ImVec4(1, 0.35f, 0.25f, 1), "Missing texture AssetId %llu",
                static_cast<unsigned long long>(sprite->texture.value));
            else
            {
                const auto texture = textures.find(sprite->texture.value);
                if (texture != textures.end() && bgfx::isValid(texture->second))
                    ImGui::Image(static_cast<ImTextureID>(texture->second.idx + 1), ImVec2(160, 120));
                else ImGui::TextDisabled("Image preview unavailable (decode/upload failed)");
                ImGui::TextWrapped("AssetId %llu — %s", static_cast<unsigned long long>(asset->id.value),
                    asset->sourceRelativePath.c_str());
            }
        }
    }
}

void layoutSceneBranch(const Scene& scene, const std::optional<EntityId> parent, const int depth,
    int& nextRow, std::vector<SceneNodeLayout>& nodes)
{
    for (const auto& entity : scene.entities())
    {
        if (entity.parent != parent) continue;
        nodes.push_back({&entity, depth, nextRow++});
        layoutSceneBranch(scene, entity.id, depth + 1, nextRow, nodes);
    }
}

void drawSceneView(const Scene& scene, std::optional<EntityId>& selected)
{
    ImGui::Text("%s — hierarchy preview", scene.name().c_str());
    ImGui::TextDisabled("Transform data is stored; this view remains structural and has no spatial rendering.");
    std::vector<SceneNodeLayout> nodes;
    int nextRow = 0;
    layoutSceneBranch(scene, std::nullopt, 0, nextRow, nodes);
    int maxDepth = 0;
    for (const auto& node : nodes) maxDepth = std::max(maxDepth, node.depth);
    const float nodeWidth = 150.0f, nodeHeight = 48.0f, xStep = 190.0f, yStep = 78.0f;
    ImGui::SetNextWindowContentSize(ImVec2(40.0f + maxDepth * xStep + nodeWidth,
        40.0f + std::max(1, nextRow) * yStep));
    if (!ImGui::BeginChild("SceneCanvas", ImVec2(0, 0), true))
    {
        ImGui::EndChild();
        return;
    }
    const auto origin = ImGui::GetCursorScreenPos();
    const auto available = ImGui::GetContentRegionAvail();
    auto* draw = ImGui::GetWindowDrawList();
    const auto clip = ImGui::GetWindowPos();
    const auto clipEnd = ImVec2(clip.x + ImGui::GetWindowSize().x, clip.y + ImGui::GetWindowSize().y);
    draw->PushClipRect(clip, clipEnd, true);
    const auto gridRight = origin.x + std::max(available.x, 320.0f + maxDepth * xStep);
    const auto gridBottom = origin.y + std::max(available.y, 80.0f + nextRow * yStep);
    for (float x = origin.x; x < gridRight; x += 32.0f)
        draw->AddLine(ImVec2(x, origin.y), ImVec2(x, gridBottom), IM_COL32(255, 255, 255, 10));
    for (float y = origin.y; y < gridBottom; y += 32.0f)
        draw->AddLine(ImVec2(origin.x, y), ImVec2(gridRight, y), IM_COL32(255, 255, 255, 10));

    auto nodePosition = [origin, xStep, yStep](const SceneNodeLayout& node) {
        return ImVec2(origin.x + 16.0f + node.depth * xStep, origin.y + 16.0f + node.row * yStep);
    };
    for (const auto& node : nodes)
    {
        if (!node.entity->parent) continue;
        const auto parent = std::find_if(nodes.begin(), nodes.end(), [&node](const SceneNodeLayout& candidate) {
            return candidate.entity->id == *node.entity->parent;
        });
        if (parent == nodes.end()) continue;
        const auto from = nodePosition(*parent), to = nodePosition(node);
        draw->AddLine(ImVec2(from.x + nodeWidth, from.y + nodeHeight * 0.5f),
            ImVec2(to.x, to.y + nodeHeight * 0.5f), IM_COL32(130, 160, 210, 220), 2.0f);
    }
    for (const auto& node : nodes)
    {
        const auto position = nodePosition(node);
        ImGui::SetCursorScreenPos(position);
        const auto id = "entity-" + std::to_string(node.entity->id);
        const bool clicked = ImGui::InvisibleButton(id.c_str(), ImVec2(nodeWidth, nodeHeight));
        const auto min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
        const auto fill = selected == node.entity->id ? IM_COL32(65, 105, 170, 255)
            : ImGui::IsItemHovered() ? IM_COL32(65, 75, 95, 255) : IM_COL32(45, 52, 65, 255);
        draw->AddRectFilled(min, max, fill, 5.0f);
        draw->AddRect(min, max, IM_COL32(145, 158, 180, 220), 5.0f);
        draw->AddText(ImVec2(min.x + 8.0f, min.y + 8.0f), IM_COL32(240, 242, 248, 255), node.entity->name.c_str());
        draw->AddText(ImVec2(min.x + 8.0f, min.y + 27.0f), IM_COL32(175, 184, 200, 255),
            ("Entity " + std::to_string(node.entity->id)).c_str());
        if (clicked) selected = node.entity->id;
    }
    draw->PopClipRect();
    ImGui::EndChild();
}

class Editor
{
    SDL_Window* window = nullptr;
    bool sdl = false, graphics = false, gui = false, platform = false;
    bgfx::ProgramHandle program = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle font = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sampler = BGFX_INVALID_HANDLE, outputMode = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout layout;
    std::map<std::uint64_t, bgfx::TextureHandle> spriteTextures;

public:
    ~Editor()
    {
        if (platform) ImGui_ImplSDL3_Shutdown();
        if (gui) ImGui::DestroyContext();
        if (graphics)
        {
            for (const auto& [id, texture] : spriteTextures)
                if (bgfx::isValid(texture)) bgfx::destroy(texture);
            if (bgfx::isValid(font)) bgfx::destroy(font);
            if (bgfx::isValid(program)) bgfx::destroy(program);
            if (bgfx::isValid(sampler)) bgfx::destroy(sampler);
            if (bgfx::isValid(outputMode)) bgfx::destroy(outputMode);
            bgfx::shutdown();
        }
        if (window) SDL_DestroyWindow(window);
        if (sdl) SDL_Quit();
    }

    bgfx::TextureHandle loadSpriteTexture(const std::filesystem::path& file)
    {
        ComPtr<IWICImagingFactory> factory;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&factory)))) return BGFX_INVALID_HANDLE;
        ComPtr<IWICBitmapDecoder> decoder;
        if (FAILED(factory->CreateDecoderFromFilename(file.c_str(), nullptr, GENERIC_READ,
            WICDecodeMetadataCacheOnLoad, &decoder))) return BGFX_INVALID_HANDLE;
        ComPtr<IWICBitmapFrameDecode> frame;
        if (FAILED(decoder->GetFrame(0, &frame))) return BGFX_INVALID_HANDLE;
        UINT width = 0, height = 0;
        if (FAILED(frame->GetSize(&width, &height)) || width == 0 || height == 0
            || width > 16384 || height > 16384 || std::uint64_t(width) * height > 64ull * 1024 * 1024)
            return BGFX_INVALID_HANDLE;
        ComPtr<IWICFormatConverter> converter;
        if (FAILED(factory->CreateFormatConverter(&converter))
            || FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA,
                WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) return BGFX_INVALID_HANDLE;
        const auto byteCount = std::uint64_t(width) * height * 4;
        if (byteCount > std::numeric_limits<UINT>::max()) return BGFX_INVALID_HANDLE;
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(byteCount));
        if (FAILED(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(byteCount), pixels.data())))
            return BGFX_INVALID_HANDLE;
        const auto* memory = bgfx::copy(pixels.data(), static_cast<std::uint32_t>(pixels.size()));
        return bgfx::createTexture2D(static_cast<std::uint16_t>(width), static_cast<std::uint16_t>(height),
            false, 1, bgfx::TextureFormat::BGRA8, BGFX_TEXTURE_NONE, memory);
    }

    void init()
    {
        if (!SDL_Init(SDL_INIT_VIDEO)) throw std::runtime_error(SDL_GetError());
        sdl = true;
        window = SDL_CreateWindow("AstroEngine Editor", 1100, 720, SDL_WINDOW_RESIZABLE);
        if (!window) throw std::runtime_error(SDL_GetError());
        bgfx::Init config;
        config.type = bgfx::RendererType::Direct3D11;
        config.fallback = false;
        config.swapChain.nwh = SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
        config.swapChain.width = 1100;
        config.swapChain.height = 720;
        config.reset = BGFX_RESET_VSYNC;
        if (!config.swapChain.nwh || !bgfx::init(config)) throw std::runtime_error("bgfx D3D11 initialization failed");
        graphics = true;
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        gui = true;
        auto& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
        io.IniFilename = nullptr; // No changes to user layout or project files.
        io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
        if (!ImGui_ImplSDL3_InitForD3D(window)) throw std::runtime_error("ImGui SDL3 backend failed");
        platform = true;
        ImGui::StyleColorsDark();
        layout.begin().add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true).end();
        static_assert(sizeof(ImDrawVert) == 20);
        program = bgfx::createProgram(
            bgfx::createShader(bgfx::copy(vs_ocornut_imgui_dxbc, sizeof(vs_ocornut_imgui_dxbc))),
            bgfx::createShader(bgfx::copy(fs_ocornut_imgui_dxbc, sizeof(fs_ocornut_imgui_dxbc))), true);
        sampler = bgfx::createUniform("s_tex", bgfx::UniformType::Sampler);
        outputMode = bgfx::createUniform("u_imguiOutput", bgfx::UniformFreq::Frame, bgfx::UniformType::Vec4);
        unsigned char* pixels = nullptr;
        int width = 0, height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        font = bgfx::createTexture2D(static_cast<uint16_t>(width), static_cast<uint16_t>(height), false, 1,
            bgfx::TextureFormat::RGBA8, 0, bgfx::copy(pixels, width * height * 4));
        if (!bgfx::isValid(program) || !bgfx::isValid(font) || !bgfx::isValid(sampler) || !bgfx::isValid(outputMode))
            throw std::runtime_error("Renderer resource creation failed");
        io.Fonts->SetTexID(static_cast<ImTextureID>(font.idx + 1));
        std::cout << "[info] SDL " << SDL_GetVersion() << "; ImGui " << ImGui::GetVersion()
                  << "; renderer " << bgfx::getRendererName(bgfx::getRendererType()) << '\n';
    }

    void draw(ImDrawData* data, int width, int height)
    {
        bgfx::setViewRect(0, 0, 0, static_cast<uint16_t>(width), static_cast<uint16_t>(height));
        bgfx::setViewClear(0, BGFX_CLEAR_COLOR, 0x18202aff);
        bgfx::setViewMode(0, bgfx::ViewMode::Sequential);
        float projection[16];
        bx::mtxOrtho(projection, data->DisplayPos.x, data->DisplayPos.x + data->DisplaySize.x,
            data->DisplayPos.y + data->DisplaySize.y, data->DisplayPos.y, 0.0f, 1000.0f, 0.0f,
            bgfx::getCaps()->homogeneousDepth);
        bgfx::setViewTransform(0, nullptr, projection);
        const float colorMode[4] = {0, 1, 0, 0};
        bgfx::setFrameUniform(outputMode, colorMode);
        bgfx::touch(0);
        for (const auto* list : data->CmdLists)
        {
            bgfx::TransientVertexBuffer vertices;
            bgfx::TransientIndexBuffer indices;
            if (!bgfx::allocTransientBuffers(&vertices, layout, list->VtxBuffer.Size, &indices, list->IdxBuffer.Size, sizeof(ImDrawIdx) == 4))
                throw std::runtime_error("Transient geometry budget exceeded");
            std::memcpy(vertices.data, list->VtxBuffer.Data, list->VtxBuffer.Size * sizeof(ImDrawVert));
            std::memcpy(indices.data, list->IdxBuffer.Data, list->IdxBuffer.Size * sizeof(ImDrawIdx));
            for (const auto& command : list->CmdBuffer)
            {
                if (command.UserCallback)
                {
                    if (command.UserCallback != ImDrawCallback_ResetRenderState) command.UserCallback(list, &command);
                    continue;
                }
                const auto clip = command.ClipRect;
                const auto scale = data->FramebufferScale;
                const float x = std::clamp((clip.x - data->DisplayPos.x) * scale.x, 0.0f, float(width));
                const float y = std::clamp((clip.y - data->DisplayPos.y) * scale.y, 0.0f, float(height));
                const float right = std::clamp((clip.z - data->DisplayPos.x) * scale.x, 0.0f, float(width));
                const float bottom = std::clamp((clip.w - data->DisplayPos.y) * scale.y, 0.0f, float(height));
                if (right <= x || bottom <= y || command.ElemCount == 0) continue;
                bgfx::setScissor(uint16_t(x), uint16_t(y), uint16_t(right - x), uint16_t(bottom - y));
                bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_MSAA |
                    BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA));
                bgfx::setTexture(0, sampler, bgfx::TextureHandle{uint16_t(command.GetTexID() - 1)});
                bgfx::setVertexBuffer(0, &vertices, command.VtxOffset, list->VtxBuffer.Size - command.VtxOffset);
                bgfx::setIndexBuffer(&indices, command.IdxOffset, command.ElemCount);
                bgfx::submit(0, program);
            }
        }
    }

    int run(bool smoke)
    {
        int width = 1100, height = 720, frames = 0;
        bool running = true, resetLayout = true, docked = false;
        bool showProject = true, showDiagnostics = true, showVisualLogic = true;
        auto logicGraph = makeScoreGateGraph();
        astro::logic::VisualLogicDebugger logicDebugger;
        bool visualLogicWindowDrawn = false;
        auto timelineCreated = astro::runtime::AnimationTimeline::create({{0.0, 0.0}, {1.0, 10.0}, {2.0, 0.0}});
        if (!timelineCreated) throw std::runtime_error(timelineCreated.error().message);
        auto timeline = std::move(timelineCreated).value();
        astro::runtime::CameraRig cameraRig;
        bool showTimeline = false, showCameraPresets = false;
        bool timelineWindowDrawn = false, cameraWindowDrawn = false;
        bool showUiEditor = false, uiEditorWindowDrawn = false;
        astro::ui::UiDocument uiDocument;
        std::optional<astro::ui::ElementId> selectedUiElement;
        std::optional<astro::ui::ElementId> uiTextDraftElement;
        std::array<char, 4097> uiTextDraft{};
        int uiPreviewPreset = 0;
        bool uiBindingErrorReported = false;
        char path[1024] = "";
        char scenePathInput[1024] = "";
        char assetPathInput[512] = "";
        char name[256] = "My Game";
        std::string activeProject, message = "Create or open a project using File.";
        Scene scene(1, "Main Scene");
        astro::core::EngineContext commandContext;
        astro::core::CommandRegistry commands(commandContext);
        const auto commandRegistration = astro::scene::registerSceneCommands(commands, scene);
        if (!commandRegistration) throw std::runtime_error(commandRegistration.error().message);
        std::optional<EntityId> selectedEntity;
        std::array<char, 256> inspectorDraft{};
        std::optional<EntityId> inspectorDraftEntity;
        std::optional<astro::scene::SceneId> inspectorDraftScene;
        std::uint64_t nextSceneId = 2;
        std::uint64_t nextEntityName = 1;
        bool sceneDirty = false;
        bool dialog = false, create = false, sceneDialog = false;
        std::filesystem::path activeProjectRoot, currentScenePath;
        std::optional<astro::assets::AssetRegistry> assetRegistry;
        std::vector<std::string> diagnostics;
        std::optional<astro::assets::AssetId> smokeSpriteAsset;
        int undoShortcuts = 0, redoShortcuts = 0, saveShortcuts = 0, openShortcuts = 0;
        auto reportError = [&](const std::string& error) {
            message = error;
            diagnostics.push_back(error);
            if (diagnostics.size() > 32) diagnostics.erase(diagnostics.begin());
        };
        auto startNewScene = [&] {
            const auto id = nextSceneId++;
            scene = Scene(id, "Scene " + std::to_string(id));
            currentScenePath.clear();
            sceneDirty = false;
            selectedEntity.reset();
            inspectorDraftEntity.reset();
            inspectorDraftScene.reset();
            commands.clearHistory();
        };
        auto saveCurrentScene = [&]() -> bool {
            if (activeProjectRoot.empty())
            {
                reportError("Open a project before saving a scene");
                return false;
            }
            auto target = currentScenePath;
            if (target.empty())
            {
                target = activeProjectRoot / "Scenes" / ("scene_" + std::to_string(scene.id()) + ".astro-scene");
                std::error_code fsError;
                const auto directory = target.parent_path();
                if (!std::filesystem::exists(directory, fsError) && !fsError)
                    std::filesystem::create_directory(directory, fsError);
                if (fsError)
                {
                    reportError("Cannot create Scenes directory: " + fsError.message());
                    return false;
                }
                const bool alreadyExists = std::filesystem::exists(target, fsError);
                if (fsError || alreadyExists)
                {
                    reportError(fsError ? "Cannot inspect scene destination: " + fsError.message()
                        : "Scene file already exists. Open it before saving changes.");
                    return false;
                }
            }
            auto saved = astro::scene::saveSceneFile(target, scene);
            if (!saved)
            {
                reportError(saved.error().message);
                return false;
            }
            currentScenePath = target;
            sceneDirty = false;
            message = "Scene saved";
            return true;
        };
        auto openSceneFile = [&](const std::filesystem::path& file) -> bool {
            if (sceneDirty)
            {
                reportError("Save the current scene before opening another scene");
                return false;
            }
            auto loaded = astro::scene::loadSceneFile(file);
            if (!loaded)
            {
                reportError(loaded.error().message);
                return false;
            }
            scene = std::move(loaded).value();
            currentScenePath = file;
            sceneDirty = false;
            selectedEntity.reset();
            inspectorDraftEntity.reset();
            inspectorDraftScene.reset();
            commands.clearHistory();
            if (scene.id() >= nextSceneId) nextSceneId = scene.id() + 1;
            message = "Scene opened";
            return true;
        };
        auto openProject = [&](const std::filesystem::path& file) {
            if (sceneDirty) throw std::runtime_error("Save the current scene before opening another project");
            auto result = astro::editor::readProject(file);
            if (!result) throw std::runtime_error(result.error().message);
            auto projectRoot = file.parent_path();
            if (projectRoot.empty()) projectRoot = std::filesystem::current_path();
            auto registry = astro::assets::AssetRegistry::open(projectRoot);
            if (!registry) throw std::runtime_error(registry.error().message);
            activeProject = result.value().name;
            activeProjectRoot = projectRoot;
            for (const auto& [id, texture] : spriteTextures)
                if (bgfx::isValid(texture)) bgfx::destroy(texture);
            spriteTextures.clear();
            assetRegistry = std::move(registry).value();
            startNewScene();
            message = "Project opened: " + result.value().name;
        };
        auto createProject = [&](const std::filesystem::path& directory, const std::string& title) {
            if (sceneDirty) throw std::runtime_error("Save the current scene before creating another project");
            auto result = astro::editor::createProject(directory, title);
            if (!result) throw std::runtime_error(result.error().message);
            auto registry = astro::assets::AssetRegistry::open(directory);
            if (!registry) throw std::runtime_error(registry.error().message);
            activeProject = result.value().name;
            activeProjectRoot = directory;
            for (const auto& [id, texture] : spriteTextures)
                if (bgfx::isValid(texture)) bgfx::destroy(texture);
            spriteTextures.clear();
            assetRegistry = std::move(registry).value();
            startNewScene();
            message = "Project created: " + result.value().name;
        };
        auto undoSceneEdit = [&] {
            if (!commands.canUndo()) { message = "Nothing to undo"; return false; }
            auto result = commands.undo();
            if (!result) { reportError(result.error().message); return false; }
            sceneDirty = true;
            message = "Undo completed";
            ++undoShortcuts;
            return true;
        };
        auto redoSceneEdit = [&] {
            if (!commands.canRedo()) { message = "Nothing to redo"; return false; }
            auto result = commands.redo();
            if (!result) { reportError(result.error().message); return false; }
            sceneDirty = true;
            message = "Redo completed";
            ++redoShortcuts;
            return true;
        };
        auto queueShortcut = [](const SDL_Keycode key) {
            SDL_Event event{};
            event.type = SDL_EVENT_KEY_DOWN;
            event.key.key = key;
            event.key.scancode = SDL_GetScancodeFromKey(key, nullptr);
            event.key.mod = SDL_KMOD_CTRL;
            event.key.down = true;
            if (!SDL_PushEvent(&event)) return false;
            event.type = SDL_EVENT_KEY_UP;
            event.key.down = false;
            return SDL_PushEvent(&event);
        };
        std::filesystem::path smokeDirectory;
        if (smoke)
        {
            const auto started = logicDebugger.start(logicGraph);
            const auto evaluated = started ? logicDebugger.runToEnd()
                : astro::core::Result<void>::failure(started.error());
            const auto* finalTrace = logicGraph.nodes().empty() ? nullptr
                : logicDebugger.trace(logicGraph.nodes().back().id);
            if (!evaluated || !finalTrace || !finalTrace->output
                || !std::holds_alternative<bool>(*finalTrace->output) || !std::get<bool>(*finalTrace->output))
                throw std::runtime_error("Visual Logic editor smoke evaluation failed");
            smokeDirectory = std::filesystem::temp_directory_path() /
                ("AstroEditor-" + std::to_string(SDL_GetTicksNS()));
            createProject(smokeDirectory, "Smoke project Ω");
            activeProject.clear();
            openProject(smokeDirectory / "project.astro");
            std::filesystem::create_directories(smokeDirectory / "Assets" / "Sprites");
            const auto smokePng = makeSmokePng();
            { std::ofstream imageFile(smokeDirectory / "Assets" / "Sprites" / "smoke.png", std::ios::binary);
              imageFile.write(reinterpret_cast<const char*>(smokePng.data()), static_cast<std::streamsize>(smokePng.size())); }
            auto importedSmokeImage = assetRegistry->importImage(std::filesystem::path("Sprites") / "smoke.png");
            if (!importedSmokeImage) throw std::runtime_error("Smoke image import failed: " + importedSmokeImage.error().message);
            smokeSpriteAsset = importedSmokeImage.value().id;
            const auto smokeTexture = loadSpriteTexture(assetRegistry->cachedPath(importedSmokeImage.value().id));
            if (!bgfx::isValid(smokeTexture)) throw std::runtime_error("Smoke image WIC decode/BGFX upload failed");
            spriteTextures[importedSmokeImage.value().id.value] = smokeTexture;
            const auto occupied = smokeDirectory / "occupied";
            std::filesystem::create_directory(occupied);
            { std::ofstream sentinel(occupied / "keep.txt", std::ios::binary); sentinel << "preserve"; }
            bool rejected = false;
            try { createProject(occupied, "Do not overwrite"); }
            catch (const std::exception&) { rejected = true; }
            std::ifstream sentinel(occupied / "keep.txt", std::ios::binary);
            const std::string sentinelContents((std::istreambuf_iterator<char>(sentinel)), {});
            if (!rejected || sentinelContents != "preserve" || activeProject != "Smoke project Ω")
                throw std::runtime_error("Project collision smoke failed");
            const auto invalidFile = smokeDirectory / "invalid.astro";
            { std::ofstream invalid(invalidFile, std::ios::binary); invalid << "ASTRO_PROJECT 999\n\\\"Invalid\\\"\n"; }
            rejected = false;
            try { openProject(invalidFile); }
            catch (const std::exception&) { rejected = true; }
            if (!rejected || activeProject != "Smoke project Ω")
                throw std::runtime_error("Invalid project must preserve the active project");
            const auto root = scene.createEntity("Smoke Root Ω");
            const auto child = root ? scene.createEntity("Smoke Child", root.value())
                : astro::core::Result<EntityId>::failure({astro::core::ErrorCode::InternalError, "Root creation failed"});
            if (!root || !child || scene.entities().size() != 2 || scene.find(child.value())->parent != root.value())
                throw std::runtime_error("Scene model smoke failed");
            selectedEntity = root.value();
            showTimeline = true;
            showCameraPresets = true;
            if (!timeline.seek(0.5) || timeline.sample() != 5.0
                || !cameraRig.setMode(astro::runtime::CameraMode::ThirdPerson))
                throw std::runtime_error("Timeline/camera preset smoke setup failed");
            auto cameraSettings = cameraRig.settings();
            cameraSettings.target = root.value();
            if (!cameraRig.configure(cameraSettings) || !cameraRig.snapToTarget(scene)
                || cameraRig.pose().position.y != 2.0 || cameraRig.pose().position.z != -5.0)
                throw std::runtime_error("Camera preset target/pose smoke failed");
            showUiEditor = true;
            const auto uiTitle = uiDocument.addElement(astro::ui::ElementKind::Text, "Player label");
            if (!uiTitle) throw std::runtime_error("UI editor smoke element creation failed");
            selectedUiElement = uiTitle.value();
            astro::ui::ResponsiveRect uiResponsive;
            uiResponsive.anchorMinX = 0.05;
            uiResponsive.anchorMinY = 0.85;
            uiResponsive.anchorMaxX = 0.95;
            uiResponsive.anchorMaxY = 0.95;
            uiResponsive.offsetMaxX = -8.0;
            uiResponsive.offsetMaxY = -8.0;
            if (!uiDocument.setLayout(uiTitle.value(), uiResponsive))
                throw std::runtime_error("UI responsive layout setup failed");
            const auto uiLayout = uiDocument.resolveLayout(uiTitle.value(), 1280.0, 720.0);
            const auto uiPhoneLayout = uiDocument.resolveLayout(uiTitle.value(), 390.0, 844.0);
            if (!uiLayout || !uiPhoneLayout || uiLayout.value().width == uiPhoneLayout.value().width)
                throw std::runtime_error("UI responsive layout smoke failed");
            if (!uiDocument.setBinding(uiTitle.value(), astro::ui::UiBinding{root.value(),
                astro::ui::BindingSource::EntityName, astro::ui::BindingTarget::Text}, scene)
                || !uiDocument.refreshBindings(scene) || uiDocument.find(uiTitle.value())->text != "Smoke Root Ω")
                throw std::runtime_error("UI scene binding smoke failed");
            const astro::scene::SetScenePropertyPayload editName{root.value(), "name", std::string{"Edited Root Ω"}};
            if (!commands.validate("entity.set_property", 1, editName)
                || !commands.execute<astro::scene::SetScenePropertyPayload, void>("entity.set_property", 1, editName)
                || scene.find(root.value())->name != "Edited Root Ω")
                throw std::runtime_error("Inspector command smoke failed");
            const astro::scene::SetSpriteRendererPayload setSprite{root.value(), importedSmokeImage.value().id, true};
            if (!commands.execute<astro::scene::SetSpriteRendererPayload, void>("entity.set_sprite_renderer", 1, setSprite)
                || !scene.spriteRenderer(root.value()))
                throw std::runtime_error("SpriteRenderer command smoke failed");
            sceneDirty = true;
            if (!saveCurrentScene() || !queueShortcut(SDLK_Z) || !queueShortcut(SDLK_Y)
                || !queueShortcut(SDLK_S) || !queueShortcut(SDLK_O))
                throw std::runtime_error("Scene shortcut smoke setup failed");
        }
        while (running && (!smoke || frames < 90))
        {
            SDL_Event event;
            while (SDL_PollEvent(&event))
            {
                ImGui_ImplSDL3_ProcessEvent(&event);
                if (event.type == SDL_EVENT_QUIT) running = false;
                if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat
                    && !ImGui::GetIO().WantCaptureKeyboard && (event.key.mod & SDL_KMOD_CTRL) != 0)
                {
                    if (event.key.key == SDLK_S)
                    {
                        if (saveCurrentScene()) ++saveShortcuts;
                    }
                    else if (event.key.key == SDLK_O)
                    {
                        sceneDialog = true;
                        ++openShortcuts;
                    }
                    else if (event.key.key == SDLK_Z)
                    {
                        if ((event.key.mod & SDL_KMOD_SHIFT) != 0) redoSceneEdit();
                        else undoSceneEdit();
                    }
                    else if (event.key.key == SDLK_Y) redoSceneEdit();
                }
            }
            int newWidth, newHeight;
            SDL_GetWindowSizeInPixels(window, &newWidth, &newHeight);
            if (newWidth <= 0 || newHeight <= 0) { SDL_Delay(16); continue; }
            if (newWidth != width || newHeight != height)
            {
                width = newWidth; height = newHeight;
                bgfx::SwapChain swapChain;
                swapChain.width = width; swapChain.height = height;
                bgfx::reset(BGFX_RESET_VSYNC, &swapChain);
            }
            if (assetRegistry)
                for (const auto& entity : scene.entities())
                    if (entity.spriteRenderer && assetRegistry->find(entity.spriteRenderer->texture)
                        && !spriteTextures.contains(entity.spriteRenderer->texture.value))
                    {
                        const auto cached = assetRegistry->cachedPath(entity.spriteRenderer->texture);
                        std::error_code cacheError;
                        if (std::filesystem::exists(cached, cacheError) && !cacheError)
                        {
                            const auto handle = loadSpriteTexture(cached);
                            if (bgfx::isValid(handle)) spriteTextures.emplace(entity.spriteRenderer->texture.value, handle);
                        }
                    }
            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();
            if (ImGui::BeginMainMenuBar())
            {
                if (ImGui::BeginMenu("File"))
                {
                    if (ImGui::MenuItem("New scene", nullptr, false, !activeProjectRoot.empty()))
                    {
                        if (sceneDirty) reportError("Save the current scene before creating another scene");
                        else { startNewScene(); message = "New scene created in memory"; }
                    }
                    if (ImGui::MenuItem("Save scene", "Ctrl+S", false, !activeProjectRoot.empty()))
                        saveCurrentScene();
                    if (ImGui::MenuItem("Open scene...", "Ctrl+O", false, !activeProjectRoot.empty()))
                        sceneDialog = true;
                    ImGui::Separator();
                    if (ImGui::MenuItem("New project...")) { dialog = true; create = true; }
                    if (ImGui::MenuItem("Open project...")) { dialog = true; create = false; }
                    if (ImGui::MenuItem("Close project", nullptr, false, !activeProject.empty()))
                    {
                        if (sceneDirty) reportError("Save the current scene before closing the project");
                        else
                        {
                            activeProject.clear();
                            activeProjectRoot.clear();
                            currentScenePath.clear();
                            commands.clearHistory();
                        }
                    }
                    if (ImGui::MenuItem("Exit")) running = false;
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Edit"))
                {
                    if (ImGui::MenuItem("Undo", "Ctrl+Z", false, commands.canUndo())) undoSceneEdit();
                    if (ImGui::MenuItem("Redo", "Ctrl+Y", false, commands.canRedo())) redoSceneEdit();
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("View"))
                {
                    ImGui::MenuItem("Project", nullptr, &showProject);
                    ImGui::MenuItem("Diagnostics", nullptr, &showDiagnostics);
                    ImGui::MenuItem("Visual Logic", nullptr, &showVisualLogic);
                    ImGui::MenuItem("UI Editor", nullptr, &showUiEditor);
                    ImGui::MenuItem("Timeline", nullptr, &showTimeline);
                    ImGui::MenuItem("Camera Presets", nullptr, &showCameraPresets);
                    if (ImGui::MenuItem("Reset layout"))
                    {
                        resetLayout = true;
                        showProject = showDiagnostics = showVisualLogic = true;
                        showUiEditor = true;
                        showTimeline = showCameraPresets = true;
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMainMenuBar();
            }
            const auto dock = ImGui::DockSpaceOverViewport();
            if (resetLayout)
            {
                ImGui::DockBuilderRemoveNode(dock);
                ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
                ImGui::DockBuilderSetNodeSize(dock, ImGui::GetMainViewport()->WorkSize);
                ImGuiID left, remainder, center, right, logicPane, scenePane;
                ImGui::DockBuilderSplitNode(dock, ImGuiDir_Left, 0.23f, &left, &remainder);
                ImGui::DockBuilderSplitNode(remainder, ImGuiDir_Right, 0.25f, &right, &center);
                ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.32f, &logicPane, &scenePane);
                ImGui::DockBuilderDockWindow("Hierarchy", left);
                ImGui::DockBuilderDockWindow("Scene View", scenePane);
                ImGui::DockBuilderDockWindow("Visual Logic", logicPane);
                ImGui::DockBuilderDockWindow("Project", right);
                ImGui::DockBuilderDockWindow("Diagnostics", right);
                ImGui::DockBuilderDockWindow("Inspector", right);
                ImGui::DockBuilderDockWindow("UI Editor", right);
                ImGui::DockBuilderDockWindow("Timeline", right);
                ImGui::DockBuilderDockWindow("Camera Presets", right);
                ImGui::DockBuilderFinish(dock);
                resetLayout = false;
            }
            if (showProject)
            {
                ImGui::Begin("Project", &showProject);
                docked = ImGui::IsWindowDocked();
                ImGui::TextUnformatted(activeProject.empty() ? "No project open" : activeProject.c_str());
                ImGui::Separator();
                ImGui::TextWrapped("Scene files are stored under Scenes. Import PNG/JPEG source files from Assets/.");
                if (assetRegistry)
                {
                    ImGui::InputText("Assets-relative image path", assetPathInput, sizeof(assetPathInput));
                    if (ImGui::Button("Import PNG/JPEG"))
                    {
                        const std::u8string encoded(reinterpret_cast<const char8_t*>(assetPathInput));
                        auto imported = assetRegistry->importImage(std::filesystem::path(encoded));
                        if (!imported) reportError(imported.error().message);
                        else
                        {
                            message = "Imported image as AssetId " + std::to_string(imported.value().id.value);
                            const auto handle = loadSpriteTexture(assetRegistry->cachedPath(imported.value().id));
                            if (!bgfx::isValid(handle)) reportError("Image imported, but WIC could not decode it for preview");
                            else spriteTextures[imported.value().id.value] = handle;
                        }
                    }
                    for (const auto missing : assetRegistry->missingAssets())
                        ImGui::TextColored(ImVec4(1, 0.35f, 0.25f, 1), "Missing source for AssetId %llu",
                            static_cast<unsigned long long>(missing.value));
                    if (ImGui::CollapsingHeader("Imported images"))
                        for (const auto& [id, ref] : assetRegistry->entries())
                            ImGui::BulletText("%llu — %s (%u x %u)", static_cast<unsigned long long>(id.value),
                                ref.sourceRelativePath.c_str(), ref.width, ref.height);
                }
                ImGui::End();
            }
            if (ImGui::Begin("Hierarchy"))
            {
                docked = ImGui::IsWindowDocked();
                ImGui::BeginDisabled(activeProjectRoot.empty());
                if (ImGui::Button("+ Entity"))
                {
                    auto created = scene.createEntity("Entity " + std::to_string(nextEntityName++));
                    if (created)
                    {
                        selectedEntity = created.value();
                        sceneDirty = true;
                        commands.clearHistory();
                        message = "Entity created";
                    }
                    else reportError(created.error().message);
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::BeginDisabled(activeProjectRoot.empty() || !selectedEntity || !scene.find(*selectedEntity));
                if (ImGui::Button("+ Child"))
                {
                    auto created = scene.createEntity("Entity " + std::to_string(nextEntityName++), selectedEntity);
                    if (created)
                    {
                        selectedEntity = created.value();
                        sceneDirty = true;
                        commands.clearHistory();
                        message = "Child entity created";
                    }
                    else reportError(created.error().message);
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::BeginDisabled(activeProjectRoot.empty() || !selectedEntity || !scene.find(*selectedEntity));
                if (ImGui::Button("Delete subtree"))
                {
                    auto erased = scene.erase(*selectedEntity, true);
                    if (erased)
                    {
                        selectedEntity.reset();
                        inspectorDraftEntity.reset();
                        sceneDirty = true;
                        commands.clearHistory();
                        message = "Entity subtree deleted";
                    }
                    else reportError(erased.error().message);
                }
                ImGui::EndDisabled();
                ImGui::Separator();
                ImGui::TextUnformatted(scene.name().c_str());
                ImGui::BeginChild("EntityTree", ImVec2(0, 0), false);
                drawHierarchyBranch(scene, std::nullopt, selectedEntity);
                ImGui::EndChild();
            }
            ImGui::End();
            if (ImGui::Begin("Inspector"))
            {
                docked = docked && ImGui::IsWindowDocked();
                if (activeProjectRoot.empty()) ImGui::TextDisabled("Create or open a project first.");
                else drawInspector(scene, selectedEntity, commands, inspectorDraft, inspectorDraftEntity,
                    inspectorDraftScene, sceneDirty, message, assetRegistry ? &*assetRegistry : nullptr,
                    spriteTextures, reportError);
            }
            ImGui::End();
            if (ImGui::Begin("Scene View"))
            {
                docked = docked && ImGui::IsWindowDocked();
                drawSceneView(scene, selectedEntity);
            }
            ImGui::End();
            if (showVisualLogic)
                drawVisualLogic(logicGraph, logicDebugger, reportError, visualLogicWindowDrawn);
            if (showUiEditor)
            {
                const auto bindings = uiDocument.refreshBindings(scene);
                if (!bindings && !uiBindingErrorReported)
                {
                    reportError(bindings.error().message);
                    uiBindingErrorReported = true;
                }
                if (bindings) uiBindingErrorReported = false;
                drawUiEditor(uiDocument, scene, selectedUiElement, uiPreviewPreset, uiTextDraft,
                    uiTextDraftElement, reportError, uiEditorWindowDrawn);
            }
            if (showTimeline)
                drawTimelineEditor(timeline, ImGui::GetIO().DeltaTime, reportError, timelineWindowDrawn);
            if (showCameraPresets)
                drawCameraPresets(cameraRig, scene, selectedEntity, ImGui::GetIO().DeltaTime,
                    reportError, cameraWindowDrawn);
            if (showDiagnostics)
            {
                ImGui::Begin("Diagnostics", &showDiagnostics);
                ImGui::TextWrapped("%s", message.c_str());
                ImGui::Text("Framebuffer: %d x %d", width, height);
                ImGui::Text("Renderer: %s", bgfx::getRendererName(bgfx::getRendererType()));
                ImGui::Text("Scene: %s%s", scene.name().c_str(), sceneDirty ? " * unsaved" : "");
                if (ImGui::CollapsingHeader("Recent diagnostics"))
                {
                    if (diagnostics.empty()) ImGui::TextDisabled("No errors recorded");
                    const auto start = diagnostics.size() > 8 ? diagnostics.size() - 8 : 0;
                    for (std::size_t index = start; index < diagnostics.size(); ++index)
                        ImGui::BulletText("%s", diagnostics[index].c_str());
                }
                const auto audit = commands.auditTrail();
                int shownErrors = 0;
                for (auto it = audit.rbegin(); it != audit.rend() && shownErrors < 4; ++it)
                {
                    if (!it->error) continue;
                    ImGui::TextWrapped("%s: %s", it->commandId.c_str(), it->error->message.c_str());
                    ++shownErrors;
                }
                ImGui::End();
            }
            if (dialog)
            {
                ImGui::Begin(create ? "New project" : "Open project", &dialog);
                ImGui::InputText(create ? "New directory (UTF-8)" : "project.astro path (UTF-8)", path, sizeof(path));
                if (create) ImGui::InputText("Project name", name, sizeof(name));
                if (ImGui::Button(create ? "Create" : "Open"))
                {
                    try
                    {
                        if (create) createProject(std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(path))), name);
                        else openProject(std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(path))));
                        dialog = false;
                    }
                    catch (const std::exception& error) { reportError(error.what()); }
                }
                ImGui::TextWrapped("%s", message.c_str());
                ImGui::End();
            }
            if (sceneDialog)
            {
                ImGui::Begin("Open Scene", &sceneDialog);
                ImGui::InputText("Scene file path (UTF-8)", scenePathInput, sizeof(scenePathInput));
                if (ImGui::Button("Open scene"))
                {
                    try
                    {
                        const auto sceneFile = std::filesystem::path(
                            std::u8string(reinterpret_cast<const char8_t*>(scenePathInput)));
                        if (openSceneFile(sceneFile)) sceneDialog = false;
                    }
                    catch (const std::exception& error) { reportError(error.what()); }
                }
                ImGui::TextWrapped("%s", message.c_str());
                ImGui::End();
            }
            ImGui::Render();
            draw(ImGui::GetDrawData(), width, height);
            bgfx::frame();
            ++frames;
        }
        const bool rendered = bgfx::getStats()->numDraw > 0;
        if (smoke)
        {
            sceneDialog = false;
            if (undoShortcuts != 1 || redoShortcuts != 1 || saveShortcuts != 1 || openShortcuts != 1
                || !openSceneFile(currentScenePath) || sceneDirty || !scene.find(1)
                || scene.find(1)->name != "Edited Root Ω"
                || !scene.spriteRenderer(1) || !smokeSpriteAsset || scene.spriteRenderer(1)->texture != *smokeSpriteAsset)
                throw std::runtime_error("Scene persistence/shortcut smoke failed");
            if (!scene.prefabInstances().empty()) throw std::runtime_error("Empty prefab metadata smoke failed");
            const auto loadedPath = currentScenePath;
            const auto invalidScenePath = smokeDirectory / "invalid.astro-scene";
            { std::ofstream invalidScene(invalidScenePath, std::ios::binary); invalidScene << "ASTRO_SCENE 999\n"; }
            if (openSceneFile(invalidScenePath) || !scene.find(1) || scene.find(1)->name != "Edited Root Ω"
                || currentScenePath != loadedPath || diagnostics.empty())
                throw std::runtime_error("Invalid scene open must preserve the active scene and report diagnostics");
        }
        std::cout << "[editor] frames=" << frames << " docked=" << docked << " rendered=" << rendered
                  << " undo=" << undoShortcuts << " redo=" << redoShortcuts << " save=" << saveShortcuts << '\n';
        // Smoke creates only these two owned paths.
        if (smoke)
        {
            std::filesystem::remove(smokeDirectory / "project.astro");
            std::filesystem::remove(smokeDirectory / "invalid.astro");
            std::filesystem::remove(smokeDirectory / "invalid.astro-scene");
            std::filesystem::remove(smokeDirectory / "occupied" / "keep.txt");
            std::filesystem::remove(smokeDirectory / "occupied");
            std::error_code ignored;
            std::filesystem::remove(currentScenePath, ignored);
            std::filesystem::remove_all(smokeDirectory / "Assets", ignored);
            std::filesystem::remove_all(smokeDirectory / "Library", ignored);
            std::filesystem::remove_all(smokeDirectory / ".astro", ignored);
            std::filesystem::remove_all(smokeDirectory / "Scenes", ignored);
            std::filesystem::remove(smokeDirectory / "project.astro");
            std::filesystem::remove(smokeDirectory);
        }
        return smoke && !(frames == 90 && docked && rendered && activeProject == "Smoke project Ω"
            && scene.entities().size() == 2 && scene.find(1)->name == "Edited Root Ω"
            && undoShortcuts == 1 && redoShortcuts == 1 && saveShortcuts == 1 && openShortcuts == 1
            && visualLogicWindowDrawn && logicDebugger.state() == astro::logic::DebugState::Completed
            && uiEditorWindowDrawn && uiDocument.elements().size() == 1
            && uiDocument.find(1)->text == "Edited Root Ω"
            && timelineWindowDrawn && timeline.keyframes().size() == 3 && timeline.sample() == 5.0
            && cameraWindowDrawn && cameraRig.settings().mode == astro::runtime::CameraMode::ThirdPerson) ? 1 : 0;
    }
};
}
int main(int argc, char** argv)
{
    if (argc > 2 || (argc == 2 && std::string_view(argv[1]) != "--smoke")) return 2;
    const auto comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(comResult)) { std::cerr << "COM initialization failed\n"; return 1; }
    struct ComLifetime { ~ComLifetime() { CoUninitialize(); } } comLifetime;
    try { Editor editor; editor.init(); return editor.run(argc == 2); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

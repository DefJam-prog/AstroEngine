#include <filesystem>
#include <fstream>
#include <string>
#include "ProjectFile.hpp"
#include "astro/assets/AssetRegistry.hpp"
#include "astro/scene/Scene.hpp"
#include "astro/scene/SceneFile.hpp"
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
        bool showProject = true, showDiagnostics = true;
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
                    if (ImGui::MenuItem("Reset layout")) { resetLayout = true; showProject = showDiagnostics = true; }
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
                ImGuiID left, remainder, center, right;
                ImGui::DockBuilderSplitNode(dock, ImGuiDir_Left, 0.23f, &left, &remainder);
                ImGui::DockBuilderSplitNode(remainder, ImGuiDir_Right, 0.25f, &right, &center);
                ImGui::DockBuilderDockWindow("Hierarchy", left);
                ImGui::DockBuilderDockWindow("Scene View", center);
                ImGui::DockBuilderDockWindow("Project", right);
                ImGui::DockBuilderDockWindow("Diagnostics", right);
                ImGui::DockBuilderDockWindow("Inspector", right);
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
            && undoShortcuts == 1 && redoShortcuts == 1 && saveShortcuts == 1 && openShortcuts == 1) ? 1 : 0;
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

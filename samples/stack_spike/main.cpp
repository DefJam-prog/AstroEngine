#include <SDL3/SDL.h>
#include <bgfx/bgfx.h>
#include <bx/math.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_sdl3.h>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string_view>

// Upstream bgfx shader binaries; licenses remain in the dependency cache.
#include <vs_ocornut_imgui.bin.h>
#include <fs_ocornut_imgui.bin.h>

namespace
{
class Experiment
{
    SDL_Window* window = nullptr;
    bool sdl = false, graphics = false, gui = false, platform = false;
    bgfx::ProgramHandle program = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle font = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sampler = BGFX_INVALID_HANDLE, outputMode = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout layout;

public:
    ~Experiment()
    {
        if (platform) ImGui_ImplSDL3_Shutdown();
        if (gui) ImGui::DestroyContext();
        if (graphics)
        {
            if (bgfx::isValid(font)) bgfx::destroy(font);
            if (bgfx::isValid(program)) bgfx::destroy(program);
            if (bgfx::isValid(sampler)) bgfx::destroy(sampler);
            if (bgfx::isValid(outputMode)) bgfx::destroy(outputMode);
            bgfx::shutdown();
        }
        if (window) SDL_DestroyWindow(window);
        if (sdl) SDL_Quit();
    }

    void init()
    {
        if (!SDL_Init(SDL_INIT_VIDEO)) throw std::runtime_error(SDL_GetError());
        sdl = true;
        window = SDL_CreateWindow("AstroEngine 00.3 - stack experiment", 1100, 720, SDL_WINDOW_RESIZABLE);
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
        int width = 1100, height = 720, clicks = 0, keys = 0, frames = 0;
        bool running = true, docked = false, resized = false;
        char text[128] = "Type here";
        float color[3] = {0.25f, 0.75f, 0.95f};
        while (running && (!smoke || frames < 90))
        {
            if (smoke && frames == 10)
            {
                SDL_Event event{};
                event.type = SDL_EVENT_KEY_DOWN;
                event.key.windowID = SDL_GetWindowID(window);
                event.key.scancode = SDL_SCANCODE_SPACE;
                event.key.key = SDLK_SPACE;
                event.key.down = true;
                SDL_PushEvent(&event);
            }
            if (smoke && frames == 12)
            {
                SDL_Event event{};
                event.type = SDL_EVENT_KEY_UP;
                event.key.windowID = SDL_GetWindowID(window);
                event.key.scancode = SDL_SCANCODE_SPACE;
                event.key.key = SDLK_SPACE;
                SDL_PushEvent(&event);
                if (!SDL_SetWindowSize(window, 1000, 680)) throw std::runtime_error(SDL_GetError());
            }
            SDL_Event event;
            while (SDL_PollEvent(&event))
            {
                ImGui_ImplSDL3_ProcessEvent(&event);
                if (event.type == SDL_EVENT_QUIT) running = false;
            }
            int newWidth = 0, newHeight = 0;
            SDL_GetWindowSizeInPixels(window, &newWidth, &newHeight);
            if (newWidth <= 0 || newHeight <= 0) { SDL_Delay(16); continue; }
            if (newWidth != width || newHeight != height)
            {
                width = newWidth; height = newHeight; resized = true;
                bgfx::SwapChain swapChain;
                swapChain.width = width;
                swapChain.height = height;
                bgfx::reset(BGFX_RESET_VSYNC, &swapChain);
            }
            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();
            if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) ++keys;
            const auto dock = ImGui::DockSpaceOverViewport();
            if (frames == 0)
            {
                ImGui::DockBuilderRemoveNode(dock);
                ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
                ImGui::DockBuilderSetNodeSize(dock, ImGui::GetMainViewport()->Size);
                ImGuiID left, right;
                ImGui::DockBuilderSplitNode(dock, ImGuiDir_Left, 0.70f, &left, &right);
                ImGui::DockBuilderDockWindow("Primitive", left);
                ImGui::DockBuilderDockWindow("Input and diagnostics", right);
                ImGui::DockBuilderFinish(dock);
            }
            ImGui::Begin("Primitive");
            docked = ImGui::IsWindowDocked();
            ImGui::TextUnformatted("SDL3 + bgfx D3D11 + Dear ImGui docking");
            const auto p = ImGui::GetCursorScreenPos();
            const auto size = ImGui::GetContentRegionAvail();
            ImGui::GetWindowDrawList()->AddTriangleFilled(
                ImVec2(p.x + size.x * 0.5f, p.y + 60),
                ImVec2(p.x + size.x * 0.15f, p.y + size.y * 0.8f),
                ImVec2(p.x + size.x * 0.85f, p.y + size.y * 0.8f),
                ImGui::ColorConvertFloat4ToU32(ImVec4(color[0], color[1], color[2], 1)));
            ImGui::End();
            ImGui::Begin("Input and diagnostics");
            if (ImGui::Button("Click test")) ++clicks;
            ImGui::Text("Clicks: %d | Space: %d", clicks, keys);
            ImGui::InputText("Text", text, sizeof(text));
            ImGui::ColorEdit3("Triangle", color);
            ImGui::Text("Framebuffer: %d x %d", width, height);
            ImGui::Text("Docked: %s", docked ? "yes" : "no");
            ImGui::TextUnformatted("Drag panel tabs to test docking.");
            ImGui::End();
            ImGui::Render();
            draw(ImGui::GetDrawData(), width, height);
            bgfx::frame();
            ++frames;
        }
        const bool rendered = bgfx::getStats()->numDraw > 0;
        std::cout << "[info] frames=" << frames << " docked=" << docked << " keys=" << keys
                  << " clicks=" << clicks << " resized=" << resized << " rendered=" << rendered << '\n';
        return smoke && !(frames == 90 && docked && keys > 0 && resized && rendered) ? 1 : 0;
    }
};
}

int main(int argc, char** argv)
{
    if (argc > 2 || (argc == 2 && std::string_view(argv[1]) != "--smoke"))
    {
        std::cerr << "Usage: astro_stack_spike [--smoke]\n";
        return 2;
    }
    try
    {
        Experiment experiment;
        experiment.init();
        return experiment.run(argc == 2);
    }
    catch (const std::exception& error)
    {
        std::cerr << "[error] " << error.what() << '\n';
        return 1;
    }
}

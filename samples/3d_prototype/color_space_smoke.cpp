#include "astro/render3d/BaseColorTexture.hpp"

#include <SDL3/SDL.h>
#include <bgfx/bgfx.h>
#include <bx/math.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdarg>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
struct Vertex final
{
    float position[3];
    std::uint8_t color[4];
    float uv[2];
    float normal[3];
};

struct Case final
{
    bgfx::ProgramHandle program = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle lightParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle lightDirection = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle lightColor = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle baseColorFactor = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle normalMatrix = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle alphaParams = BGFX_INVALID_HANDLE;
    bool lit = false;
    bool textured = false;
    std::array<std::uint8_t, 3> expected{};
};

class Capture final : public bgfx::CallbackI
{
public:
    std::atomic<bool> done{false};
    std::atomic<bool> valid{false};
    std::array<std::uint32_t, 5> matches{};
    std::array<std::array<std::uint32_t, 3>, 5> mean{};
    std::array<std::array<std::uint8_t, 3>, 5> expected{{{{93, 93, 93}}, {{66, 66, 66}},
        {{128, 128, 128}}, {{66, 66, 66}}, {{92, 92, 92}}}};
    std::filesystem::path output;

    void fatal(const char* file, std::uint16_t line, bgfx::Fatal::Enum, const char* message) override
    { std::fprintf(stderr, "bgfx fatal %s:%u: %s\n", file ? file : "?", line, message ? message : "?"); }
    void traceVargs(const char*, std::uint16_t, const char* format, std::va_list args) override
    { std::vfprintf(stderr, format, args); }
    void profilerBegin(const char*, std::uint32_t, const char*, std::uint16_t) override {}
    void profilerBeginLiteral(const char*, std::uint32_t, const char*, std::uint16_t) override {}
    void profilerEnd() override {}
    std::uint32_t cacheReadSize(std::uint64_t) override { return 0; }
    bool cacheRead(std::uint64_t, void*, std::uint32_t) override { return false; }
    void cacheWrite(std::uint64_t, const void*, std::uint32_t) override {}
    void captureBegin(std::uint32_t, std::uint32_t, std::uint32_t, bgfx::TextureFormat::Enum, bool) override {}
    void captureEnd() override {}
    void captureFrame(const void*, std::uint32_t) override {}

    void screenShot(const char*, const std::uint32_t width, const std::uint32_t height,
        const std::uint32_t pitch, bgfx::TextureFormat::Enum, const void* data,
        const std::uint32_t size, bool) override
    {
        if (!data || width != 1200 || height != 320 || pitch < width * 4u
            || static_cast<std::uint64_t>(pitch) * height > size)
        {
            done.store(true, std::memory_order_release);
            return;
        }
        const auto* pixels = static_cast<const std::uint8_t*>(data);
        std::array<std::array<std::uint64_t, 3>, 5> sums{};
        // Each 240x320 view uses the same centered quad; inspect only its interior.
        for (std::size_t view = 0; view < 5; ++view)
        {
            const auto left = static_cast<std::uint32_t>(view) * 240u;
            for (std::uint32_t y = 104; y < 216; ++y)
            for (std::uint32_t x = left + 56; x < left + 184; ++x)
            {
                const auto* p = pixels + static_cast<std::size_t>(y) * pitch + x * 4u;
                const auto r = p[2], g = p[1], b = p[0];
                const auto& target = expected[view];
                if (std::abs(static_cast<int>(r) - target[0]) <= 3
                    && std::abs(static_cast<int>(g) - target[1]) <= 3
                    && std::abs(static_cast<int>(b) - target[2]) <= 3)
                {
                    ++matches[view];
                    sums[view][0] += r; sums[view][1] += g; sums[view][2] += b;
                }
            }
            if (matches[view] != 0)
                for (std::size_t channel = 0; channel < 3; ++channel)
                    mean[view][channel] = static_cast<std::uint32_t>(sums[view][channel] / matches[view]);
        }
        bool okay = true;
        for (const auto count : matches) okay = okay && count >= 1000;
        valid.store(okay, std::memory_order_release);
        if (!output.empty() && width <= 65535 && height <= 65535)
        {
            std::ofstream file(output, std::ios::binary | std::ios::trunc);
            const std::array<std::uint8_t, 18> header{0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                static_cast<std::uint8_t>(width), static_cast<std::uint8_t>(width >> 8),
                static_cast<std::uint8_t>(height), static_cast<std::uint8_t>(height >> 8), 32, 0x28};
            file.write(reinterpret_cast<const char*>(header.data()), static_cast<std::streamsize>(header.size()));
            for (std::uint32_t y = 0; y < height && file; ++y)
                file.write(reinterpret_cast<const char*>(pixels + static_cast<std::size_t>(pitch) * y),
                    static_cast<std::streamsize>(static_cast<std::size_t>(width) * 4));
        }
        done.store(true, std::memory_order_release);
    }
};

std::vector<std::uint8_t> readBytes(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Cannot read production BGFX shader: " + path.string());
    std::vector<std::uint8_t> data(std::istreambuf_iterator<char>(file), {});
    if (data.empty() || data.size() > 4 * 1024 * 1024)
        throw std::runtime_error("Production BGFX shader has invalid size: " + path.string());
    return data;
}

bgfx::ShaderHandle shader(const std::filesystem::path& path)
{
    auto bytes = readBytes(path);
    return bgfx::createShader(bgfx::copy(bytes.data(), static_cast<std::uint32_t>(bytes.size())));
}

bgfx::ProgramHandle program(const std::filesystem::path& dir, const char* vertex, const char* fragment)
{
    const auto vs = shader(dir / vertex);
    const auto fs = shader(dir / fragment);
    if (!bgfx::isValid(vs) || !bgfx::isValid(fs)) throw std::runtime_error("Could not create BGFX shader handles");
    const auto result = bgfx::createProgram(vs, fs, true);
    if (!bgfx::isValid(result)) throw std::runtime_error("Could not link production BGFX color shader");
    return result;
}

bgfx::UniformHandle uniform(const char* name, const bgfx::UniformType::Enum type, const std::uint16_t count = 1)
{
    const auto result = bgfx::createUniform(name, type, count);
    if (!bgfx::isValid(result)) throw std::runtime_error(std::string("Could not create BGFX uniform: ") + name);
    return result;
}
}

int main()
{
    SDL_Window* window = nullptr;
    bool sdlStarted = false, bgfxStarted = false;
    std::vector<bgfx::ProgramHandle> programs;
    std::vector<bgfx::UniformHandle> uniforms;
    bgfx::VertexBufferHandle vb = BGFX_INVALID_HANDLE, compositeVb = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle ib = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle texture = BGFX_INVALID_HANDLE, colorTarget = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle depthTarget = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle frameBuffer = BGFX_INVALID_HANDLE;
    Capture callback;
    try
    {
        if (const char* path = SDL_GetEnvironmentVariable(SDL_GetEnvironment(), "ASTRO_COLOR_CONTRACT_SCREENSHOT"))
            callback.output = std::filesystem::path(std::u8string(
                reinterpret_cast<const char8_t*>(path), std::strlen(path)));
        if (!SDL_Init(SDL_INIT_VIDEO)) throw std::runtime_error(SDL_GetError());
        sdlStarted = true;
        window = SDL_CreateWindow("AstroEngine BGFX color contract", 1200, 320, SDL_WINDOW_HIDDEN);
        if (!window) throw std::runtime_error(SDL_GetError());
        bgfx::Init init;
        init.type = bgfx::RendererType::Direct3D11;
        init.fallback = false;
        init.swapChain.nwh = SDL_GetPointerProperty(SDL_GetWindowProperties(window),
            SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
        init.swapChain.width = 1200;
        init.swapChain.height = 320;
        init.reset = BGFX_RESET_NONE;
        init.callback = &callback;
        if (!init.swapChain.nwh || !bgfx::init(init)) throw std::runtime_error("BGFX D3D11 initialization failed");
        bgfxStarted = true;

        const auto formatCaps = bgfx::getCaps()->formats[bgfx::TextureFormat::BGRA8];
        if ((formatCaps & BGFX_CAPS_FORMAT_TEXTURE_2D_SRGB) == 0
            || (formatCaps & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) == 0)
            throw std::runtime_error("D3D11 does not advertise sRGB BGRA8 framebuffer support");
        colorTarget = bgfx::createTexture2D(240, 320, false, 1, bgfx::TextureFormat::BGRA8,
            BGFX_TEXTURE_RT | BGFX_TEXTURE_SRGB_MUTABLE | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
        depthTarget = bgfx::createTexture2D(240, 320, false, 1, bgfx::TextureFormat::D24S8, BGFX_TEXTURE_RT);
        bgfx::Attachment attachments[2];
        attachments[0].init(colorTarget, bgfx::Access::Write, 0, 1, 0, BGFX_ATTACHMENT_SRGB);
        attachments[1].init(depthTarget, bgfx::Access::Write, 0, 1, 0, BGFX_ATTACHMENT_NONE);
        frameBuffer = bgfx::createFrameBuffer(2, attachments, false);
        if (!bgfx::isValid(colorTarget) || !bgfx::isValid(depthTarget) || !bgfx::isValid(frameBuffer))
            throw std::runtime_error("Could not create the sRGB BGRA8 color-contract target");

        const std::filesystem::path shaderDir(ASTRO_COLOR_CONTRACT_SHADER_DIR);
        std::array<Case, 4> cases{};
        cases[0].program = program(shaderDir, "vs_stencil_texture.bin", "fs_scene_textured.bin");
        cases[1].program = program(shaderDir, "vs_scene_lit_textured.bin", "fs_scene_lit_textured.bin");
        cases[2].program = program(shaderDir, "vs_cubes.bin", "fs_scene_color.bin");
        cases[3].program = program(shaderDir, "vs_scene_lit_color.bin", "fs_scene_lit_color.bin");
        const auto alphaParams = uniform("u_alphaParams", bgfx::UniformType::Vec4); uniforms.push_back(alphaParams);
        const auto lightParams = uniform("u_lightParams", bgfx::UniformType::Vec4); uniforms.push_back(lightParams);
        const auto lightDirection = uniform("u_lightDirection", bgfx::UniformType::Vec4, 4); uniforms.push_back(lightDirection);
        const auto lightColor = uniform("u_lightColor", bgfx::UniformType::Vec4, 4); uniforms.push_back(lightColor);
        const auto baseColorFactor = uniform("u_baseColorFactor", bgfx::UniformType::Vec4); uniforms.push_back(baseColorFactor);
        const auto normalMatrixHandle = uniform("u_normalMatrix", bgfx::UniformType::Mat3); uniforms.push_back(normalMatrixHandle);
        for (auto& c : cases)
        {
            programs.push_back(c.program);
            c.alphaParams = alphaParams;
            c.lit = (&c == &cases[1] || &c == &cases[3]);
            c.textured = (&c == &cases[0] || &c == &cases[1]);
            if (c.lit)
            {
                c.lightParams = lightParams;
                c.lightDirection = lightDirection;
                c.lightColor = lightColor;
                c.baseColorFactor = baseColorFactor;
                c.normalMatrix = normalMatrixHandle;
            }
        }
        const std::array<std::uint8_t, 4> source{128, 128, 128, 255}; // BGRA sRGB sample
        const std::array<float, 4> halfFactor{0.5f, 0.5f, 0.5f, 1.0f};
        const auto linear = astro::render3d::prepareLinearBaseColorBgra8(source, halfFactor);
        if (!linear || linear.value() != std::vector<std::uint8_t>({28, 28, 28, 255}))
            throw std::runtime_error("CPU preparation did not produce the expected linear texture sample (28)");
        texture = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::BGRA8,
            BGFX_TEXTURE_NONE, bgfx::copy(linear.value().data(), 4));
        const auto sampler = uniform("s_texColor", bgfx::UniformType::Sampler); uniforms.push_back(sampler);

        bgfx::VertexLayout layout;
        layout.begin().add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float).end();
        const std::array<Vertex, 4> vertices{{
            Vertex{{-2.0f, -1.15f, 0.0f}, {55, 55, 55, 255}, {0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
            Vertex{{ 2.0f, -1.15f, 0.0f}, {55, 55, 55, 255}, {1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
            Vertex{{ 2.0f,  1.15f, 0.0f}, {55, 55, 55, 255}, {1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},
            Vertex{{-2.0f,  1.15f, 0.0f}, {55, 55, 55, 255}, {0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}}}};
        const std::array<std::uint16_t, 6> indices{0, 1, 2, 0, 2, 3};
        vb = bgfx::createVertexBuffer(bgfx::copy(vertices.data(), sizeof(vertices)), layout);
        ib = bgfx::createIndexBuffer(bgfx::copy(indices.data(), sizeof(indices)));
        if (!bgfx::isValid(texture) || !bgfx::isValid(vb) || !bgfx::isValid(ib))
            throw std::runtime_error("BGFX could not allocate color-contract smoke resources");

        Capture& capture = callback;
        float view[16], projection[16];
        bx::mtxLookAt(view, bx::Vec3{0.0f, 0.0f, 4.0f}, bx::Vec3{0.0f, 0.0f, 0.0f});
        bx::mtxProj(projection, 60.0f, 0.75f, 0.1f, 100.0f, bgfx::getCaps()->homogeneousDepth);
        const float normalMatrix[9]{1,0,0, 0,1,0, 0,0,1};
        const float directions[16]{0,0,-1,0, 0,0,0,0, 0,0,0,0, 0,0,0,0};
        const float colors[16]{0.5f,0.5f,0.5f,1, 0,0,0,0, 0,0,0,0, 0,0,0,0};
        const float litParams[4]{1,0,0,0}, unlitParams[4]{0,0,0,0};
        const float factorWhite[4]{1,1,1,1}, factorHalf[4]{0.5f,0.5f,0.5f,1};
        const float opaque[4]{0,0.5f,0,0};
        float model[16];
        bx::mtxIdentity(model);
        const std::array<Vertex, 4> alphaVertices{{
            Vertex{{-2.0f, -1.15f, 0.0f}, {55, 55, 55, 128}, {0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
            Vertex{{ 2.0f, -1.15f, 0.0f}, {55, 55, 55, 128}, {1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
            Vertex{{ 2.0f,  1.15f, 0.0f}, {55, 55, 55, 128}, {1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},
            Vertex{{-2.0f,  1.15f, 0.0f}, {55, 55, 55, 128}, {0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}}}};
        const auto alphaVb = bgfx::createVertexBuffer(bgfx::copy(alphaVertices.data(), sizeof(alphaVertices)), layout);
        if (!bgfx::isValid(alphaVb)) throw std::runtime_error("Could not allocate alpha-blend smoke geometry");
        const std::array<Vertex, 4> compositeVertices{{
            Vertex{{960.0f, 0.0f, 0.0f}, {255,255,255,255}, {0.0f, 0.0f}, {0.0f,0.0f,1.0f}},
            Vertex{{1200.0f, 0.0f, 0.0f}, {255,255,255,255}, {1.0f, 0.0f}, {0.0f,0.0f,1.0f}},
            Vertex{{1200.0f, 320.0f, 0.0f}, {255,255,255,255}, {1.0f, 1.0f}, {0.0f,0.0f,1.0f}},
            Vertex{{960.0f, 320.0f, 0.0f}, {255,255,255,255}, {0.0f, 1.0f}, {0.0f,0.0f,1.0f}}}};
        compositeVb = bgfx::createVertexBuffer(bgfx::copy(compositeVertices.data(), sizeof(compositeVertices)), layout);
        if (!bgfx::isValid(compositeVb)) throw std::runtime_error("Could not allocate framebuffer composition geometry");
        for (std::uint32_t frame = 0; frame < 120 && !capture.done.load(std::memory_order_acquire); ++frame)
        {
            for (std::uint8_t i = 0; i < 4; ++i)
            {
                const auto viewId = static_cast<bgfx::ViewId>(i);
                bgfx::setViewRect(viewId, static_cast<std::uint16_t>(i * 240u), 0, 240, 320);
                bgfx::setViewFrameBuffer(viewId, BGFX_INVALID_HANDLE);
                bgfx::setViewClear(viewId, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x202020ff, 1.0f, 0);
                bgfx::setViewTransform(viewId, view, projection);
                bgfx::touch(viewId);
                auto& c = cases[i];
                bgfx::setTransform(model);
                bgfx::setVertexBuffer(0, vb);
                bgfx::setIndexBuffer(ib);
                const float encodedOutput[4]{0.0f, 0.5f, 0.0f, 0.0f};
                bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z
                    | BGFX_STATE_DEPTH_TEST_LESS);
                bgfx::setUniform(c.alphaParams, encodedOutput);
                if (c.textured)
                    bgfx::setTexture(0, sampler, texture, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP
                        | BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT);
                if (c.lit)
                {
                    bgfx::setUniform(c.lightParams, litParams);
                    bgfx::setUniform(c.lightDirection, directions, 4);
                    bgfx::setUniform(c.lightColor, colors, 4);
                    bgfx::setUniform(c.baseColorFactor, c.textured ? factorWhite : factorHalf);
                    bgfx::setUniform(c.normalMatrix, normalMatrix);
                }
                bgfx::submit(viewId, c.program);
            }
            const bgfx::ViewId blendView = 4;
            bgfx::setViewName(blendView, "linear-light alpha blend");
            bgfx::setViewFrameBuffer(blendView, frameBuffer);
            bgfx::setViewRect(blendView, 0, 0, 240, 320);
            bgfx::setViewClear(blendView, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x000000ff, 1.0f, 0);
            bgfx::setViewTransform(blendView, view, projection);
            bgfx::touch(blendView);
            const float blendParams[4]{2.0f, 0.5f, 1.0f, 0.0f};
            bgfx::setUniform(cases[2].alphaParams, blendParams);
            bgfx::setTransform(model);
            bgfx::setVertexBuffer(0, alphaVb);
            bgfx::setIndexBuffer(ib);
            bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z
                | BGFX_STATE_DEPTH_TEST_LESS
                | BGFX_STATE_BLEND_FUNC_SEPARATE(BGFX_STATE_BLEND_SRC_ALPHA,
                    BGFX_STATE_BLEND_INV_SRC_ALPHA, BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA));
            bgfx::submit(blendView, cases[2].program);

            const bgfx::ViewId compositeView = 5;
            bgfx::setViewName(compositeView, "sRGB attachment composition");
            bgfx::setViewFrameBuffer(compositeView, BGFX_INVALID_HANDLE);
            bgfx::setViewRect(compositeView, 0, 0, 1200, 320);
            bgfx::setViewClear(compositeView, BGFX_CLEAR_NONE);
            bgfx::setViewMode(compositeView, bgfx::ViewMode::Sequential);
            float compositeProjection[16];
            bx::mtxOrtho(compositeProjection, 0.0f, 1200.0f, 320.0f, 0.0f,
                0.0f, 100.0f, 0.0f, bgfx::getCaps()->homogeneousDepth);
            bgfx::setViewTransform(compositeView, nullptr, compositeProjection);
            bgfx::touch(compositeView);
            const float compositeAlpha[4]{0.0f, 0.5f, 1.0f, 0.0f};
            bgfx::setUniform(alphaParams, compositeAlpha);
            bgfx::setTexture(0, sampler, colorTarget, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
            bgfx::setVertexBuffer(0, compositeVb, 0, 4);
            bgfx::setIndexBuffer(ib);
            bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
            bgfx::submit(compositeView, cases[0].program);
            if (frame == 8 && !callback.output.empty())
                bgfx::requestScreenShot(BGFX_INVALID_HANDLE, callback.output.string().c_str());
            else if (frame == 8)
                bgfx::requestScreenShot(BGFX_INVALID_HANDLE, "color-contract-memory-capture");
            bgfx::frame();
        }
        const char* labels[5]{"unlit_texture", "lit_texture", "unlit_vertex", "lit_vertex", "alpha_blend"};
        for (std::size_t i = 0; i < 5; ++i)
            std::cout << labels[i] << " expected=" << static_cast<unsigned>(callback.expected[i][0])
                << " mean=" << callback.mean[i][0] << ',' << callback.mean[i][1] << ',' << callback.mean[i][2]
                << " matched=" << callback.matches[i] << '\n';
        if (!callback.done.load(std::memory_order_acquire) || !callback.valid.load(std::memory_order_acquire))
            throw std::runtime_error("Numeric BGFX screenshot did not match all four production color paths");

        bgfx::destroy(texture);
        bgfx::destroy(alphaVb);
        bgfx::destroy(compositeVb);
        bgfx::destroy(ib);
        bgfx::destroy(vb);
        bgfx::destroy(frameBuffer);
        bgfx::destroy(colorTarget);
        bgfx::destroy(depthTarget);
        for (const auto handle : uniforms) bgfx::destroy(handle);
        for (const auto handle : programs) bgfx::destroy(handle);
        bgfx::shutdown(); bgfxStarted = false;
        SDL_DestroyWindow(window); window = nullptr;
        SDL_Quit(); sdlStarted = false;
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "[error] " << e.what() << '\n';
        if (bgfxStarted)
        {
            if (bgfx::isValid(texture)) bgfx::destroy(texture);
            if (bgfx::isValid(compositeVb)) bgfx::destroy(compositeVb);
            if (bgfx::isValid(frameBuffer)) bgfx::destroy(frameBuffer);
            if (bgfx::isValid(colorTarget)) bgfx::destroy(colorTarget);
            if (bgfx::isValid(depthTarget)) bgfx::destroy(depthTarget);
            if (bgfx::isValid(ib)) bgfx::destroy(ib);
            if (bgfx::isValid(vb)) bgfx::destroy(vb);
            for (const auto handle : uniforms) if (bgfx::isValid(handle)) bgfx::destroy(handle);
            for (const auto handle : programs) if (bgfx::isValid(handle)) bgfx::destroy(handle);
            bgfx::shutdown();
        }
        if (window) SDL_DestroyWindow(window);
        if (sdlStarted) SDL_Quit();
        return 1;
    }
}

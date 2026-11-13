#include "astro/runtime/SDLImageTextureCache.hpp"

#include <SDL3/SDL_surface.h>
#include <objbase.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace astro::runtime
{
namespace
{
using Microsoft::WRL::ComPtr;
using SdlSurface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;

core::Error imageError(std::string message)
{
    return {core::ErrorCode::IoError, std::move(message)};
}

core::Result<std::vector<std::uint8_t>> decodeWic(const std::filesystem::path& path,
    std::uint32_t& width, std::uint32_t& height)
{
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool uninitialize = SUCCEEDED(initialized);
    if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE)
        return core::Result<std::vector<std::uint8_t>>::failure(imageError("WIC COM initialization failed"));
    struct ComGuard final { bool active; ~ComGuard() { if (active) CoUninitialize(); } } guard{uninitialize};

    ComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory))))
        return core::Result<std::vector<std::uint8_t>>::failure(imageError("Cannot create WIC imaging factory"));
    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
        WICDecodeMetadataCacheOnLoad, &decoder)))
        return core::Result<std::vector<std::uint8_t>>::failure(imageError("WIC cannot decode the cached image asset"));
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, &frame)) || FAILED(frame->GetSize(&width, &height))
        || width == 0 || height == 0 || width > 16384 || height > 16384
        || std::uint64_t(width) * height > 16ull * 1024 * 1024)
        return core::Result<std::vector<std::uint8_t>>::failure(imageError("WIC image dimensions exceed supported limits"));
    const auto byteCount = std::uint64_t(width) * height * 4;
    if (byteCount > (std::numeric_limits<UINT>::max)())
        return core::Result<std::vector<std::uint8_t>>::failure(imageError("WIC image buffer is too large"));
    ComPtr<IWICFormatConverter> converter;
    if (FAILED(factory->CreateFormatConverter(&converter))
        || FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA,
            WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)))
        return core::Result<std::vector<std::uint8_t>>::failure(imageError("WIC cannot convert image to BGRA8"));
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(byteCount));
    if (FAILED(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(byteCount), pixels.data())))
        return core::Result<std::vector<std::uint8_t>>::failure(imageError("WIC cannot read image pixels"));
    return core::Result<std::vector<std::uint8_t>>::success(std::move(pixels));
}
}

SDLImageTextureCache::SDLImageTextureCache(SDL_Renderer* renderer,
    const assets::AssetRegistry& registry) noexcept : renderer_(renderer), registry_(&registry) {}

SDLImageTextureCache::~SDLImageTextureCache() { clear(); }

void SDLImageTextureCache::TextureDeleter::operator()(SDL_Texture* value) const noexcept
{
    if (value) SDL_DestroyTexture(value);
}

void SDLImageTextureCache::clear() noexcept { textures_.clear(); }

core::Result<SDL_Texture*> SDLImageTextureCache::texture(const assets::AssetId id)
{
    if (!renderer_ || !registry_)
        return core::Result<SDL_Texture*>::failure({core::ErrorCode::InvalidState,
            "SDL image texture cache has no renderer or asset registry"});
    if (const auto found = textures_.find(id); found != textures_.end())
        return core::Result<SDL_Texture*>::success(found->second.get());
    if (!registry_->find(id))
        return core::Result<SDL_Texture*>::failure({core::ErrorCode::NotFound,
            "Sprite AssetId is absent from the asset registry"});
    std::uint32_t width = 0, height = 0;
    auto decoded = decodeWic(registry_->cachedPath(id), width, height);
    if (!decoded) return core::Result<SDL_Texture*>::failure(decoded.error());
    SdlSurface surface(SDL_CreateSurfaceFrom(static_cast<int>(width), static_cast<int>(height),
        SDL_PIXELFORMAT_BGRA32, decoded.value().data(), static_cast<int>(width * 4)), SDL_DestroySurface);
    if (!surface)
        return core::Result<SDL_Texture*>::failure(imageError(std::string("SDL surface creation failed: ") + SDL_GetError()));
    auto* created = SDL_CreateTextureFromSurface(renderer_, surface.get());
    if (!created)
        return core::Result<SDL_Texture*>::failure(imageError(std::string("SDL texture creation failed: ") + SDL_GetError()));
    std::unique_ptr<SDL_Texture, TextureDeleter> owned(created);
    auto* result = owned.get();
    textures_.emplace(id, std::move(owned));
    return core::Result<SDL_Texture*>::success(result);
}
}

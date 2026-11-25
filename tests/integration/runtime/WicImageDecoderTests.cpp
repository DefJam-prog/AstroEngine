#include "astro/assets/AssetRegistry.hpp"
#include "astro/runtime/WicImageDecoder.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace
{
int failures = 0;

void check(const bool condition, const char* message)
{
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
}

int main()
{
    using namespace astro;
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() / ("Astro WIC decode " + std::to_string(unique));
    try
    {
        const auto source = root / "Assets" / "Textures" / "uv-checker.png";
        std::filesystem::create_directories(source.parent_path());
        std::filesystem::copy_file(ASTRO_WIC_FIXTURE_PATH, source);
        auto registry = assets::AssetRegistry::open(root);
        check(static_cast<bool>(registry), "temporary project asset registry opens");
        if (registry)
        {
            auto imageRef = registry.value().importImage(std::filesystem::path("Textures") / "uv-checker.png");
            check(static_cast<bool>(imageRef), "fixture image validates and registers");
            if (imageRef)
            {
                auto decoded = runtime::decodeWicImage(registry.value().cachedPath(imageRef.value().id));
                check(static_cast<bool>(decoded), "registered content-addressed image cache decodes");
                if (decoded)
                {
                    const auto& image = decoded.value();
                    check(image.width == 4 && image.height == 4, "decoded dimensions are exact");
                    check(image.pixels.size() == 4u * 4u * 4u, "decoded BGRA8 allocation matches dimensions");
                    if (image.pixels.size() >= 16)
                    {
                        check(image.pixels[0] == 0 && image.pixels[1] == 0
                            && image.pixels[2] == 255 && image.pixels[3] == 255,
                            "first BGRA pixel preserves the red test texel");
                        const auto* green = image.pixels.data() + (3u * 4u);
                        check(green[0] == 0 && green[1] == 255 && green[2] == 0 && green[3] == 255,
                            "second BGRA pixel preserves the green test texel");
                    }
                }
            }
        }
        const auto corrupt = root / "corrupt.png";
        { std::ofstream output(corrupt, std::ios::binary); output << "not an image"; }
        check(!runtime::decodeWicImage(corrupt), "corrupt image is rejected");
        check(!runtime::decodeWicImage(root / "missing.png"), "missing image is rejected");
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: unexpected exception: " << error.what() << '\n';
        ++failures;
    }
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::cout << "WIC decoder integration: " << failures << " failure(s)\n";
    return failures == 0 ? 0 : 1;
}

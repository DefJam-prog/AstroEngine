#include "astro/render2d/SceneSprites.hpp"
#include "astro/scene/SceneFile.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace
{
int failures = 0;
#define CHECK(expression) do { if (!(expression)) { std::cerr << "FAIL: " #expression "\n"; ++failures; } } while (false)

void appendBe32(std::vector<std::uint8_t>& out, const std::uint32_t value)
{
    out.push_back(static_cast<std::uint8_t>(value >> 24));
    out.push_back(static_cast<std::uint8_t>(value >> 16));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value));
}

std::uint32_t crc32(const std::uint8_t* bytes, const std::size_t count)
{
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < count; ++i)
    {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

void addChunk(std::vector<std::uint8_t>& png, const std::array<char, 4> type,
    const std::vector<std::uint8_t>& data)
{
    appendBe32(png, static_cast<std::uint32_t>(data.size()));
    const auto start = png.size();
    for (const auto ch : type) png.push_back(static_cast<std::uint8_t>(ch));
    png.insert(png.end(), data.begin(), data.end());
    appendBe32(png, crc32(png.data() + start, png.size() - start));
}

std::vector<std::uint8_t> png2x3()
{
    std::vector<std::uint8_t> png{137,80,78,71,13,10,26,10};
    std::vector<std::uint8_t> ihdr;
    appendBe32(ihdr, 2); appendBe32(ihdr, 3);
    ihdr.insert(ihdr.end(), {8,6,0,0,0});
    addChunk(png, {'I','H','D','R'}, ihdr);
    std::vector<std::uint8_t> raw;
    for (int y = 0; y < 3; ++y)
    {
        raw.push_back(0);
        for (int x = 0; x < 2; ++x) raw.insert(raw.end(), {200,100,50,255});
    }
    std::vector<std::uint8_t> zlib{0x78,0x01,0x01};
    const auto length = static_cast<std::uint16_t>(raw.size());
    zlib.push_back(static_cast<std::uint8_t>(length)); zlib.push_back(static_cast<std::uint8_t>(length >> 8));
    const auto inverse = static_cast<std::uint16_t>(~length);
    zlib.push_back(static_cast<std::uint8_t>(inverse)); zlib.push_back(static_cast<std::uint8_t>(inverse >> 8));
    zlib.insert(zlib.end(), raw.begin(), raw.end());
    std::uint32_t a = 1, b = 0;
    for (const auto byte : raw) { a = (a + byte) % 65521; b = (b + a) % 65521; }
    appendBe32(zlib, (b << 16) | a);
    addChunk(png, {'I','D','A','T'}, zlib);
    addChunk(png, {'I','E','N','D'}, {});
    return png;
}

void writeProjectFixture(const std::filesystem::path& root)
{
    std::error_code error;
    if (!std::filesystem::create_directory(root, error) || error)
        throw std::runtime_error("Fixture project directory already exists or cannot be created");
    auto registryResult = astro::assets::AssetRegistry::open(root);
    if (!registryResult) throw std::runtime_error(registryResult.error().message);
    auto registry = std::move(registryResult).value();
    const auto source = root / "Assets" / "sprite.png";
    const auto bytes = png2x3();
    std::ofstream output(source, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    output.close();
    auto imported = registry.importImage("sprite.png");
    if (!imported) throw std::runtime_error(imported.error().message);
    astro::scene::Scene preview(77, "Project Preview");
    const auto parent = preview.createEntity("Preview Parent");
    if (!parent) throw std::runtime_error(parent.error().message);
    auto parentTransform = preview.transform(parent.value())->value;
    parentTransform.position = {0.0, 0.0, 0.0};
    parentTransform.scale = {1.5, 1.5, 1.0};
    if (!preview.setTransform(parent.value(), parentTransform)) throw std::runtime_error("Cannot set fixture parent transform");
    const auto sprite = preview.createEntity("Preview Sprite", parent.value());
    if (!sprite) throw std::runtime_error(sprite.error().message);
    auto spriteTransform = preview.transform(sprite.value())->value;
    spriteTransform.scale = {10.0, 10.0, 1.0};
    if (!preview.setTransform(sprite.value(), spriteTransform)
        || !preview.setSpriteRenderer(sprite.value(), imported.value().id))
        throw std::runtime_error("Cannot configure fixture sprite");
    std::filesystem::create_directories(root / "Scenes", error);
    if (error) throw std::runtime_error("Cannot create fixture Scenes directory");
    const auto saved = astro::scene::saveSceneFile(root / "Scenes" / "Preview.scene", preview);
    if (!saved) throw std::runtime_error(saved.error().message);
}
}

int main(const int argc, char** argv)
{
    if (argc == 3 && std::string_view(argv[1]) == "--write-fixture")
    {
        try { writeProjectFixture(std::filesystem::path(argv[2])); return EXIT_SUCCESS; }
        catch (const std::exception& exception) { std::cerr << "FAIL: fixture: " << exception.what() << '\n'; return EXIT_FAILURE; }
    }
    if (argc != 1) return EXIT_FAILURE;
    using namespace astro;
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() / ("Astro Scene Sprite Tests " + std::to_string(unique));
    std::error_code error;
    if (!std::filesystem::create_directory(root, error) || error)
    {
        std::cerr << "FAIL: create unique test project directory\n";
        return EXIT_FAILURE;
    }
    try
    {
        auto registryResult = assets::AssetRegistry::open(root);
        CHECK(registryResult);
        if (!registryResult)
            throw std::runtime_error("AssetRegistry open failed: " + registryResult.error().message);
        auto registry = std::move(registryResult).value();
        const auto source = root / "Assets" / "sprite.png";
        const auto bytes = png2x3();
        std::ofstream output(source, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        output.close();
        auto imported = registry.importImage("sprite.png");
        CHECK(imported && imported.value().width == 2 && imported.value().height == 3);
        if (!imported) throw std::runtime_error("PNG import failed");

        scene::Scene sourceScene(9, "Sprite Scene");
        const auto parent = sourceScene.createEntity("Sprite Parent");
        if (!parent) throw std::runtime_error("Cannot create fixture parent");
        const auto visible = sourceScene.createEntity("Visible Sprite", parent.value());
        const auto hidden = sourceScene.createEntity("Hidden Sprite");
        CHECK(parent && visible && hidden);
        if (!parent || !visible || !hidden) throw std::runtime_error("Cannot create fixture entities");
        auto parentTransform = sourceScene.transform(parent.value())->value;
        parentTransform.position = {1.0, 2.0, 0.0};
        parentTransform.rotationDegrees.z = 90.0;
        parentTransform.scale = {-2.0, 1.0, 1.0};
        CHECK(sourceScene.setTransform(parent.value(), parentTransform));
        auto transform = sourceScene.transform(visible.value())->value;
        transform.position = {1.0, 0.0, 0.0};
        transform.rotationDegrees.z = 45.0;
        transform.scale = {1.0, 1.0, 1.0};
        CHECK(sourceScene.setTransform(visible.value(), transform));
        CHECK(sourceScene.setSpriteRenderer(visible.value(), imported.value().id, true));
        CHECK(sourceScene.setSpriteRenderer(hidden.value(), imported.value().id, false));

        render2d::Canvas2D canvas;
        CHECK(render2d::appendSceneSprites(sourceScene, registry, canvas, {20.0, 4}));
        CHECK(canvas.sprites().size() == 1);
        if (!canvas.sprites().empty())
        {
            const auto& sprite = canvas.sprites().front();
            CHECK(sprite.asset == imported.value().id);
            CHECK(std::abs(sprite.center.x - 1.0) < 1e-12 && std::abs(sprite.center.y) < 1e-12);
            CHECK(sprite.hasWorldCorners);
            CHECK(std::abs(sprite.worldCorners[0].x - 0.9823223304703363) < 1e-12);
            CHECK(std::abs(sprite.worldCorners[0].y - 0.1767766952966369) < 1e-12);
            CHECK(sprite.flipX && sprite.layer == 4);
        }

        render2d::Canvas2D invalidPpu;
        CHECK(!render2d::appendSceneSprites(sourceScene, registry, invalidPpu, {0.0, 0}));
        scene::Scene missingScene(10, "Missing asset");
        const auto validBeforeMissing = missingScene.createEntity("Valid before missing");
        const auto missing = missingScene.createEntity("Missing Sprite");
        CHECK(validBeforeMissing && missing);
        CHECK(validBeforeMissing && missingScene.setSpriteRenderer(validBeforeMissing.value(), imported.value().id));
        CHECK(missing && missingScene.setSpriteRenderer(missing.value(), {9001}));
        render2d::Canvas2D missingCanvas;
        CHECK(!render2d::appendSceneSprites(missingScene, registry, missingCanvas));
        CHECK(missingCanvas.sprites().empty());
        scene::Scene childScene(11, "Nested sprite");
        const auto nestedParent = childScene.createEntity("Parent");
        if (!nestedParent) throw std::runtime_error("Cannot create hierarchy fixture parent");
        const auto child = childScene.createEntity("Child", nestedParent.value());
        CHECK(child && childScene.setSpriteRenderer(child.value(), imported.value().id));
        auto nestedParentTransform = childScene.transform(nestedParent.value())->value;
        nestedParentTransform.position = {2.0, 3.0, 0.0};
        nestedParentTransform.rotationDegrees.z = 90.0;
        CHECK(childScene.setTransform(nestedParent.value(), nestedParentTransform));
        auto nestedTransform = childScene.transform(child.value())->value;
        nestedTransform.position = {1.0, 0.0, 0.0};
        CHECK(childScene.setTransform(child.value(), nestedTransform));
        render2d::Canvas2D childCanvas;
        CHECK(render2d::appendSceneSprites(childScene, registry, childCanvas));
        CHECK(childCanvas.sprites().size() == 1);
        CHECK(std::abs(childCanvas.sprites().front().center.x - 2.0) < 1e-12
            && std::abs(childCanvas.sprites().front().center.y - 4.0) < 1e-12);
        CHECK(childCanvas.sprites().front().hasWorldCorners);
        CHECK(std::filesystem::exists(source));
    }
    catch (const std::exception& exception) { std::cerr << "FAIL: " << exception.what() << '\n'; ++failures; }
    std::filesystem::remove_all(root, error);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

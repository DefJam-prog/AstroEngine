#include "astro/assets/AssetRegistry.hpp"
#include "astro/assets/AssetBuild.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
int failures = 0;

void check(const bool condition, const char* message)
{
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}

void appendBe32(std::vector<unsigned char>& output, const std::uint32_t value)
{
    output.push_back(static_cast<unsigned char>(value >> 24));
    output.push_back(static_cast<unsigned char>(value >> 16));
    output.push_back(static_cast<unsigned char>(value >> 8));
    output.push_back(static_cast<unsigned char>(value));
}

std::uint32_t crc32(const unsigned char* data, const std::size_t size)
{
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t index = 0; index < size; ++index)
    {
        crc ^= data[index];
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

void addChunk(std::vector<unsigned char>& png, const std::array<char, 4> type, const std::vector<unsigned char>& data)
{
    appendBe32(png, static_cast<std::uint32_t>(data.size()));
    const auto start = png.size();
    for (const auto character : type) png.push_back(static_cast<unsigned char>(character));
    png.insert(png.end(), data.begin(), data.end());
    appendBe32(png, crc32(png.data() + start, png.size() - start));
}

std::vector<unsigned char> makePng(const std::uint32_t width, const std::uint32_t height)
{
    std::vector<unsigned char> png{137,80,78,71,13,10,26,10};
    std::vector<unsigned char> ihdr;
    appendBe32(ihdr, width); appendBe32(ihdr, height);
    ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});
    addChunk(png, {'I','H','D','R'}, ihdr);

    std::vector<unsigned char> raw;
    for (std::uint32_t y = 0; y < height; ++y)
    {
        raw.push_back(0);
        for (std::uint32_t x = 0; x < width; ++x) raw.insert(raw.end(), {255, 0, 0, 255});
    }
    std::vector<unsigned char> zlib{0x78, 0x01, 0x01};
    const auto length = static_cast<std::uint16_t>(raw.size());
    zlib.push_back(static_cast<unsigned char>(length)); zlib.push_back(static_cast<unsigned char>(length >> 8));
    const auto inverse = static_cast<std::uint16_t>(~length);
    zlib.push_back(static_cast<unsigned char>(inverse)); zlib.push_back(static_cast<unsigned char>(inverse >> 8));
    zlib.insert(zlib.end(), raw.begin(), raw.end());
    std::uint32_t a = 1, b = 0;
    for (const auto value : raw) { a = (a + value) % 65521; b = (b + a) % 65521; }
    appendBe32(zlib, (b << 16) | a);
    addChunk(png, {'I','D','A','T'}, zlib);
    addChunk(png, {'I','E','N','D'}, {});
    return png;
}

std::vector<unsigned char> makeJpeg(const std::uint16_t width, const std::uint16_t height)
{
    return {0xFF,0xD8, 0xFF,0xC0, 0x00,0x11, 0x08,
        static_cast<unsigned char>(height >> 8), static_cast<unsigned char>(height),
        static_cast<unsigned char>(width >> 8), static_cast<unsigned char>(width),
        0x03, 0x01,0x11,0x00, 0x02,0x11,0x00, 0x03,0x11,0x00,
        0xFF,0xDA, 0x00,0x08, 0x01,0x01,0x00,0x00,0x3F,0x00, 0x00, 0xFF,0xD9};
}

void writeBytes(const std::filesystem::path& path, const std::vector<unsigned char>& bytes)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!output) throw std::runtime_error("failed to write asset fixture");
}

std::vector<unsigned char> readBytes(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::filesystem::path utf8Path(const std::u8string_view value)
{
    return std::filesystem::path(std::u8string(value));
}
}

int main()
{
    using namespace astro::assets;
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() / ("Astro Asset Tests " + std::to_string(unique));
    std::error_code error;
    const bool created = std::filesystem::create_directory(root, error);
    check(created && !error, "create a unique test-owned project directory");
    if (!created || error) return EXIT_FAILURE;

    try
    {
        auto opened = AssetRegistry::open(root);
        check(opened.hasValue(), "open an asset registry for a project");
        if (!opened) throw std::runtime_error("AssetRegistry::open failed");
        auto registry = std::move(opened).value();
        const auto pngPath = root / "Assets" / utf8Path(u8"картинка.png");
        const auto jpegPath = root / "Assets" / "background.jpeg";
        const auto textPath = root / "Assets" / "notes.txt";
        const auto invalidPath = root / "Assets" / "bad.png";
        const auto invalidJpegPath = root / "Assets" / "bad.jpg";
        const auto initialPng = makePng(1, 1);
        const auto jpeg = makeJpeg(3, 2);
        writeBytes(pngPath, initialPng);
        writeBytes(jpegPath, jpeg);
        writeBytes(textPath, {'n','o','t',' ','a','n',' ','i','m','a','g','e'});
        auto brokenPng = initialPng; brokenPng[29] ^= 0x01;
        writeBytes(invalidPath, brokenPng);
        writeBytes(invalidJpegPath, {0xFF,0xD8,0xFF,0xD9});
        const auto originalSourceBytes = readBytes(pngPath);

        auto imported = registry.importImage(utf8Path(u8"картинка.png"));
        if (!imported) std::cerr << "PNG import error: " << imported.error().message << '\n';
        check(imported && imported.value().id.value != 0 && imported.value().width == 1 && imported.value().height == 1,
            "import a UTF-8 PNG and extract its dimensions");
        if (imported)
        {
            const auto reference = imported.value();
            AssetDependencyGraph graph;
            const auto jpegReference = registry.importImage(std::filesystem::path("background.jpeg"));
            check(jpegReference && graph.addDependency(reference.id, jpegReference.value().id), "record a direct asset dependency");
            check(!graph.addDependency(jpegReference.value().id, reference.id), "reject a dependency cycle");
            const auto graphPath = root / ".astro" / "assets.dependencies";
            check(static_cast<bool>(graph.save(graphPath)), "persist dependency graph");
            auto loadedGraph = AssetDependencyGraph::load(graphPath);
            check(loadedGraph && loadedGraph.value().closure({reference.id})
                && loadedGraph.value().closure({reference.id}).value().size() == 2, "load graph and resolve transitive closure");
            if (!loadedGraph) throw std::runtime_error("dependency graph fixture failed to load");
            auto package = buildAssetPackage(registry, loadedGraph.value(), {reference.id}, AssetPlatform::Windows);
            check(package && package.value().assetCount == 2 && !package.value().reused, "build platform-tagged package of reachable assets");
            if (package) {
                const auto firstHash = package.value().sha256;
                auto again = buildAssetPackage(registry, loadedGraph.value(), {reference.id}, AssetPlatform::Windows);
                check(again && again.value().reused && again.value().sha256 == firstHash, "reuse identical derived package");
                writeBytes(package.value().path, {'b','a','d'});
                auto repaired = buildAssetPackage(registry, loadedGraph.value(), {reference.id}, AssetPlatform::Windows);
                check(repaired && !repaired.value().reused && readBytes(repaired.value().path).size() > 3, "replace a corrupted derived package");
                auto otherTarget = buildAssetPackage(registry, loadedGraph.value(), {reference.id}, AssetPlatform::Android);
                check(otherTarget && otherTarget.value().sha256 != firstHash, "target label participates in package bytes");
            }
            check(reference.sha256.size() == 64, "record a SHA-256 content digest");
            check(std::filesystem::exists(registry.cachedPath(reference.id)), "write encoded image to project cache");
            check(readBytes(registry.cachedPath(reference.id)) == originalSourceBytes, "cache preserves source bytes");
            check(readBytes(pngPath) == originalSourceBytes, "import does not modify the source image");
            auto repeated = registry.importImage(utf8Path(u8"картинка.png"));
            check(repeated && repeated.value().id == reference.id && registry.entries().size() == 2,
                "repeat import reuses the stable asset ID");

            writeBytes(pngPath, makePng(2, 1));
            check(!registry.importImage(utf8Path(u8"картинка.png")),
                "changed source requires explicit reimport");
            auto reimported = registry.reimport(reference.id);
            check(reimported && reimported.value().id == reference.id && reimported.value().width == 2
                && reimported.value().sha256 != reference.sha256,
                "reimport updates metadata and content while preserving Asset ID");
            if (reimported && package)
            {
                auto rebuilt = buildAssetPackage(registry, loadedGraph.value(), {reference.id}, AssetPlatform::Windows);
                check(rebuilt && rebuilt.value().sha256 != package.value().sha256
                    && loadedGraph.value().dependenciesOf(reference.id) == std::vector<AssetId>{jpegReference.value().id},
                    "reimport preserves graph references and rebuilds changed package bytes");
            }
            check(readBytes(pngPath) == makePng(2, 1), "reimport leaves updated source bytes untouched");
            if (reimported)
            {
                const auto cached = registry.cachedPath(reference.id);
                const auto symlinkTarget = cached.parent_path() / "cache-link-check.png";
                std::error_code symlinkError;
                std::filesystem::create_symlink(pngPath, symlinkTarget, symlinkError);
                if (!symlinkError)
                {
                    std::filesystem::remove(cached, error);
                    std::filesystem::create_symlink(pngPath, cached, symlinkError);
                    if (!symlinkError)
                    {
                        check(!registry.reimport(reference.id), "reject cache symlink instead of reading outside cache artifact");
                        std::filesystem::remove(cached, error);
                    }
                    std::filesystem::remove(symlinkTarget, error);
                }
                auto preparedCache = registry.reimport(reference.id);
                check(preparedCache.hasValue(), "recreate or validate cache after the symlink check");
                const auto safeCache = registry.cachedPath(reference.id);
                auto damaged = readBytes(safeCache);
                if (!damaged.empty())
                {
                    damaged.front() ^= 0x01; writeBytes(safeCache, damaged);
                    check(registry.reimport(reference.id).hasValue() && readBytes(safeCache) == readBytes(pngPath),
                        "reimport repairs a damaged cache artifact");
                }
                else check(false, "resolve the regular cache file before corruption test");
            }
            auto reopened = AssetRegistry::open(root);
            check(reopened && reopened.value().find(reference.id)
                && reopened.value().find(reference.id)->width == 2,
                "persist and reload asset IDs and metadata");
            if (reopened)
            {
                auto movedSource = pngPath; movedSource += ".temporarily-missing";
                std::filesystem::rename(pngPath, movedSource, error);
                const auto missing = reopened.value().missingAssets();
                check(!error && std::find(missing.begin(), missing.end(), reference.id) != missing.end(),
                    "report a registered asset whose source is missing");
                check(!reopened.value().reimport(reference.id), "reject reimport when source is missing");
                std::filesystem::rename(movedSource, pngPath, error);
                check(!error, "restore the exact temporary test source path");
            }
            const auto indexPath = root / ".astro" / "assets.index";
            auto indexBytes = readBytes(indexPath);
            std::vector<unsigned char> crlf;
            for (const auto character : indexBytes)
            {
                if (character == '\n') crlf.push_back('\r');
                crlf.push_back(character);
            }
            writeBytes(indexPath, crlf);
            auto crlfIndex = AssetRegistry::open(root);
            check(crlfIndex && crlfIndex.value().find(reference.id), "read the UTF-8 index with Windows CRLF endings");
        }

        auto importedJpeg = registry.importImage(std::filesystem::path("background.jpeg"));
        check(importedJpeg && importedJpeg.value().format == ImageFormat::Jpeg
            && importedJpeg.value().width == 3 && importedJpeg.value().height == 2,
            "import JPEG and read its frame dimensions");
        check(!registry.importImage(std::filesystem::path("../outside.png")), "reject path traversal outside Assets");
        check(!registry.importImage(std::filesystem::path("notes.txt")), "reject unsupported file extension");
        check(!registry.importImage(std::filesystem::path("bad.png")), "reject invalid PNG chunk checksum");
        check(!registry.importImage(std::filesystem::path("bad.jpg")), "reject JPEG without a frame header");
        check(!registry.reimport({999999}), "report a missing Asset ID");
        const auto indexPath = root / ".astro" / "assets.index";
        writeBytes(indexPath, {'B','A','D','\n'});
        check(!AssetRegistry::open(root), "reject corrupt index without silently replacing it");
        check(readBytes(indexPath) == std::vector<unsigned char>{'B','A','D','\n'},
            "leave a corrupt user index unchanged for diagnosis");
    }
    catch (const std::exception& exception)
    {
        std::cerr << "UNEXPECTED: " << exception.what() << '\n';
        ++failures;
    }

    std::filesystem::remove_all(root, error);
    check(!error, "remove only the test-owned project directory");
    if (failures == 0)
    {
        std::cout << "All asset registry integration tests passed.\n";
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}

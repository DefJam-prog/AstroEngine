#include "astro/assets/AssetBuild.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <fstream>
#include <limits>
#include <sstream>
#include <system_error>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace astro::assets
{
namespace
{
constexpr std::uint64_t MaxPackageBytes = 1024ull * 1024ull * 1024ull;
core::Error err(core::ErrorCode code, std::string message) { return {code, std::move(message)}; }
void appendU32(std::vector<std::uint8_t>& out, std::uint32_t v)
{ for (int s = 0; s < 32; s += 8) out.push_back(static_cast<std::uint8_t>(v >> s)); }
void appendU64(std::vector<std::uint8_t>& out, std::uint64_t v)
{ for (int s = 0; s < 64; s += 8) out.push_back(static_cast<std::uint8_t>(v >> s)); }
void appendText(std::vector<std::uint8_t>& out, std::string_view s)
{ out.insert(out.end(), s.begin(), s.end()); }

core::Result<void> atomicWrite(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
{
    static std::atomic<std::uint64_t> tempCounter{0};
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) return core::Result<void>::failure(err(core::ErrorCode::IoError, "Cannot create derived resource directory: " + ec.message()));
    auto temp = path;
#ifdef _WIN32
    temp += L".tmp." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(tempCounter.fetch_add(1));
#else
    temp += ".tmp." + std::to_string(tempCounter.fetch_add(1));
#endif
    { std::ofstream out(temp, std::ios::binary | std::ios::trunc);
      if (!out || !out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())) || !out.flush())
          return core::Result<void>::failure(err(core::ErrorCode::IoError, "Cannot write temporary package")); }
#ifdef _WIN32
    if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    { DeleteFileW(temp.c_str()); return core::Result<void>::failure(err(core::ErrorCode::IoError, "Cannot atomically publish package (Windows error " + std::to_string(GetLastError()) + ")")); }
#else
    std::filesystem::rename(temp, path, ec);
    if (ec) { std::filesystem::remove(temp); return core::Result<void>::failure(err(core::ErrorCode::IoError, "Cannot atomically publish package: " + ec.message())); }
#endif
    return core::Result<void>::success();
}

core::Result<std::vector<std::uint8_t>> readFile(const std::filesystem::path& path)
{
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size > MaxPackageBytes || size > std::numeric_limits<std::size_t>::max())
        return core::Result<std::vector<std::uint8_t>>::failure(err(core::ErrorCode::IoError, "Cannot read or package file exceeds 1 GiB"));
    std::ifstream in(path, std::ios::binary);
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    if (!in || !in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size())) || in.peek() != std::char_traits<char>::eof())
        return core::Result<std::vector<std::uint8_t>>::failure(err(core::ErrorCode::IoError, "Cannot read package input completely"));
    return core::Result<std::vector<std::uint8_t>>::success(std::move(data));
}
}

core::Result<void> AssetDependencyGraph::addDependency(AssetId asset, AssetId dependency)
{
    if (!asset.value || !dependency.value || asset == dependency)
        return core::Result<void>::failure(err(core::ErrorCode::InvalidArgument, "Dependency IDs must be non-zero and distinct"));
    if (edges_[asset].contains(dependency)) return core::Result<void>::success();
    edges_[asset].insert(dependency);
    auto check = closure({asset});
    if (!check) { edges_[asset].erase(dependency); if (edges_[asset].empty()) edges_.erase(asset); return core::Result<void>::failure(check.error()); }
    return core::Result<void>::success();
}

bool AssetDependencyGraph::removeAsset(AssetId asset)
{
    const bool existed = edges_.erase(asset) != 0;
    for (auto& [_, deps] : edges_) deps.erase(asset);
    return existed;
}

std::vector<AssetId> AssetDependencyGraph::dependenciesOf(AssetId asset) const
{
    const auto it = edges_.find(asset);
    return it == edges_.end() ? std::vector<AssetId>{} : std::vector<AssetId>(it->second.begin(), it->second.end());
}

core::Result<std::vector<AssetId>> AssetDependencyGraph::closure(const std::vector<AssetId>& roots) const
{
    enum class Mark { Visiting, Done };
    std::map<AssetId, Mark> marks;
    std::set<AssetId> result;
    const auto visit = [&](const auto& self, AssetId id) -> bool {
        if (!id.value) return false;
        auto found = marks.find(id);
        if (found != marks.end()) return found->second == Mark::Done;
        marks[id] = Mark::Visiting;
        const auto edge = edges_.find(id);
        if (edge != edges_.end()) for (const auto dep : edge->second) {
            const auto mark = marks.find(dep);
            if (mark != marks.end() && mark->second == Mark::Visiting) return false;
            if (!self(self, dep)) return false;
        }
        marks[id] = Mark::Done; result.insert(id); return true;
    };
    for (const auto id : roots) if (!visit(visit, id))
        return core::Result<std::vector<AssetId>>::failure(err(core::ErrorCode::DependencyError, "Invalid ID or cycle in asset dependency graph"));
    return core::Result<std::vector<AssetId>>::success({result.begin(), result.end()});
}

core::Result<void> AssetDependencyGraph::save(const std::filesystem::path& path) const
{
    std::ostringstream text; text.imbue(std::locale::classic()); text << "ASTRO_DEPENDENCIES 1\n";
    for (const auto& [id, deps] : edges_) { text << "NODE " << id.value << ' ' << deps.size() << '\n'; for (const auto dep : deps) text << "DEP " << dep.value << '\n'; }
    text << "END\n"; const auto s = text.str();
    return atomicWrite(path, std::vector<std::uint8_t>(s.begin(), s.end()));
}

core::Result<AssetDependencyGraph> AssetDependencyGraph::load(const std::filesystem::path& path)
{
    auto bytes = readFile(path);
    if (!bytes || bytes.value().size() > 16 * 1024 * 1024) return core::Result<AssetDependencyGraph>::failure(bytes ? err(core::ErrorCode::ValidationError, "Dependency graph exceeds 16 MiB") : bytes.error());
    std::string text(bytes.value().begin(), bytes.value().end()); std::istringstream in(text); in.imbue(std::locale::classic());
    std::string token; int version = 0;
    if (!(in >> token >> version) || token != "ASTRO_DEPENDENCIES" || version != 1)
        return core::Result<AssetDependencyGraph>::failure(err(core::ErrorCode::VersionMismatch, "Unsupported dependency graph format"));
    AssetDependencyGraph graph; std::size_t nodes = 0, edges = 0;
    while (in >> token) {
        if (token == "END") {
            if (in >> token) return core::Result<AssetDependencyGraph>::failure(err(core::ErrorCode::ParseError, "Trailing graph data"));
            std::vector<AssetId> roots; for (const auto& [id, _] : graph.edges_) roots.push_back(id);
            const auto valid = graph.closure(roots);
            if (!valid) return core::Result<AssetDependencyGraph>::failure(valid.error());
            return core::Result<AssetDependencyGraph>::success(std::move(graph));
        }
        std::uint64_t id = 0; std::size_t count = 0;
        if (token != "NODE" || !(in >> id >> count) || !id || count > 100000 || ++nodes > 100000 || graph.edges_.contains({id}))
            return core::Result<AssetDependencyGraph>::failure(err(core::ErrorCode::ParseError, "Invalid dependency node"));
        for (std::size_t i = 0; i < count; ++i) {
            std::uint64_t dep = 0; if (!(in >> token >> dep) || token != "DEP" || !dep || dep == id || ++edges > 500000)
                return core::Result<AssetDependencyGraph>::failure(err(core::ErrorCode::ParseError, "Invalid dependency edge"));
            if (!graph.edges_[{id}].insert({dep}).second) return core::Result<AssetDependencyGraph>::failure(err(core::ErrorCode::ParseError, "Duplicate dependency edge"));
        }
    }
    return core::Result<AssetDependencyGraph>::failure(err(core::ErrorCode::ParseError, "Dependency graph has no END"));
}

std::string_view assetPlatformName(AssetPlatform platform) noexcept
{
    switch (platform) { case AssetPlatform::Windows: return "windows"; case AssetPlatform::MacOS: return "macos"; case AssetPlatform::Android: return "android"; case AssetPlatform::IOS: return "ios"; }
    return "unknown";
}

core::Result<AssetPackageResult> buildAssetPackage(const AssetRegistry& registry, const AssetDependencyGraph& graph,
    const std::vector<AssetId>& roots, AssetPlatform platform)
{
    const auto closure = graph.closure(roots);
    if (!closure) return core::Result<AssetPackageResult>::failure(closure.error());
    const auto platformName = assetPlatformName(platform);
    if (platformName == "unknown") return core::Result<AssetPackageResult>::failure(err(core::ErrorCode::InvalidArgument, "Unknown asset platform"));
    std::vector<std::uint8_t> data; appendText(data, "ASTRO_PACK"); appendU32(data, 1); appendU32(data, static_cast<std::uint32_t>(platformName.size())); appendText(data, platformName); appendU32(data, static_cast<std::uint32_t>(closure.value().size()));
    for (const auto id : closure.value()) {
        const auto* ref = registry.find(id); if (!ref) return core::Result<AssetPackageResult>::failure(err(core::ErrorCode::NotFound, "Package graph references an unregistered AssetId " + std::to_string(id.value)));
        const auto source = registry.projectRoot() / "Assets" / std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(ref->sourceRelativePath.data()), ref->sourceRelativePath.size()));
        const auto cached = registry.cachedPath(id); auto bytes = readFile(cached);
        if (cached.empty() || !bytes || bytes.value().size() != ref->sourceSize || sha256Hex(bytes.value()) != ref->sha256)
            return core::Result<AssetPackageResult>::failure(err(core::ErrorCode::ValidationError, "Asset cache is missing or corrupt; explicitly reimport AssetId " + std::to_string(id.value)));
        std::error_code ec; const auto sourceBytes = readFile(source);
        if (!sourceBytes || sourceBytes.value().size() != ref->sourceSize || sha256Hex(sourceBytes.value()) != ref->sha256)
            return core::Result<AssetPackageResult>::failure(err(core::ErrorCode::ValidationError, "Asset source changed or disappeared; explicitly reimport AssetId " + std::to_string(id.value)));
        if (data.size() + bytes.value().size() > MaxPackageBytes) return core::Result<AssetPackageResult>::failure(err(core::ErrorCode::ValidationError, "Asset package exceeds 1 GiB"));
        appendU64(data, id.value); appendU32(data, ref->format == ImageFormat::Png ? 1u : 2u); appendU32(data, ref->width); appendU32(data, ref->height); appendU64(data, bytes.value().size()); appendText(data, ref->sha256); data.insert(data.end(), bytes.value().begin(), bytes.value().end());
    }
    const auto hash = sha256Hex(data); const auto path = registry.projectRoot() / "Library" / "Derived" / std::string(platformName) / (hash + ".v1.astro-pack");
    std::error_code ec;
    if (std::filesystem::is_regular_file(path, ec) && !ec) { auto old = readFile(path); if (old && old.value() == data) return core::Result<AssetPackageResult>::success({path, hash, std::string(platformName), closure.value().size(), data.size(), true}); }
    const auto written = atomicWrite(path, data); if (!written) return core::Result<AssetPackageResult>::failure(written.error());
    return core::Result<AssetPackageResult>::success({path, hash, std::string(platformName), closure.value().size(), data.size(), false});
}
}

#include "astro/prefabs/PrefabLibrary.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <functional>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace astro::prefabs
{
namespace
{
constexpr std::uintmax_t MaxPrefabFileBytes = 16u * 1024u * 1024u;
constexpr std::size_t MaxPrefabNodes = 100000;
constexpr std::size_t MaxExpandedNodes = 100000;
constexpr std::size_t MaxNestedDepth = 64;
constexpr std::size_t MaxPrefabHierarchyDepth = 256;

core::Result<void> failure(const core::ErrorCode code, std::string message)
{
    return core::Result<void>::failure({code, std::move(message)});
}

template<class T>
core::Result<T> resultFailure(const core::ErrorCode code, std::string message)
{
    return core::Result<T>::failure({code, std::move(message)});
}

bool hasExtra(std::istringstream& input)
{
    std::string extra;
    return static_cast<bool>(input >> extra);
}

bool validUtf8(const std::string_view value)
{
    for (std::size_t i = 0; i < value.size();)
    {
        const auto lead = static_cast<unsigned char>(value[i]);
        if (lead <= 0x7F) { ++i; continue; }
        const auto continuation = [&value](const std::size_t index) {
            return index < value.size() && (static_cast<unsigned char>(value[index]) & 0xC0) == 0x80;
        };
        if (lead >= 0xC2 && lead <= 0xDF && continuation(i + 1)) { i += 2; continue; }
        if (lead >= 0xE0 && lead <= 0xEF && continuation(i + 1) && continuation(i + 2))
        {
            const auto second = static_cast<unsigned char>(value[i + 1]);
            if ((lead != 0xE0 || second >= 0xA0) && (lead != 0xED || second < 0xA0)) { i += 3; continue; }
        }
        if (lead >= 0xF0 && lead <= 0xF4 && continuation(i + 1) && continuation(i + 2) && continuation(i + 3))
        {
            const auto second = static_cast<unsigned char>(value[i + 1]);
            if ((lead != 0xF0 || second >= 0x90) && (lead != 0xF4 || second <= 0x8F)) { i += 4; continue; }
        }
        return false;
    }
    return true;
}

std::filesystem::path prefabPath(const std::filesystem::path& directory, const PrefabId id)
{
    return directory / (std::to_string(id) + ".astro-prefab");
}

std::string serializePrefab(const PrefabAsset& prefab)
{
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::setprecision(std::numeric_limits<double>::max_digits10);
    output << "ASTRO_PREFAB 1\nPREFAB " << prefab.id << ' ' << std::quoted(prefab.name) << '\n';
    for (const auto& node : prefab.nodes)
    {
        output << "NODE " << node.localId << ' ' << node.parentLocalId << ' ' << std::quoted(node.name) << ' '
               << node.transform.position.x << ' ' << node.transform.position.y << ' ' << node.transform.position.z << ' '
               << node.transform.rotationDegrees.x << ' ' << node.transform.rotationDegrees.y << ' ' << node.transform.rotationDegrees.z << ' '
               << node.transform.scale.x << ' ' << node.transform.scale.y << ' ' << node.transform.scale.z << ' '
               << (node.sprite ? 1 : 0);
        if (node.sprite) output << ' ' << node.sprite->texture.value << ' ' << (node.sprite->visible ? 1 : 0);
        output << ' ' << node.nestedPrefab << '\n';
    }
    output << "END\n";
    return output.str();
}

core::Result<void> writeAtomically(const std::filesystem::path& file, const std::string& contents)
{
    static std::atomic<std::uint64_t> sequence{0};
    std::filesystem::path temporary;
#ifdef _WIN32
    HANDLE handle = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 32; ++attempt)
    {
        temporary = file;
        temporary += L".tmp." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(GetTickCount64())
            + L"." + std::to_wstring(sequence.fetch_add(1, std::memory_order_relaxed));
        handle = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
            FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_WRITE_THROUGH, nullptr);
        if (handle != INVALID_HANDLE_VALUE) break;
        const auto error = GetLastError();
        if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS)
            return failure(core::ErrorCode::IoError, "Cannot reserve temporary prefab file");
    }
    if (handle == INVALID_HANDLE_VALUE) return failure(core::ErrorCode::IoError, "Cannot reserve temporary prefab file");
    DWORD written = 0;
    const bool ok = WriteFile(handle, contents.data(), static_cast<DWORD>(contents.size()), &written, nullptr)
        && written == static_cast<DWORD>(contents.size()) && FlushFileBuffers(handle);
    const bool closed = CloseHandle(handle) != 0;
    if (!ok || !closed)
    {
        DeleteFileW(temporary.c_str());
        return failure(core::ErrorCode::IoError, "Cannot flush temporary prefab file");
    }
    if (!MoveFileExW(temporary.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        DeleteFileW(temporary.c_str());
        return failure(core::ErrorCode::IoError, "Cannot atomically replace prefab file");
    }
#else
    for (int attempt = 0; attempt < 32; ++attempt)
    {
        temporary = file;
        temporary += ".tmp." + std::to_string(sequence.fetch_add(1, std::memory_order_relaxed));
        const int descriptor = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (descriptor < 0)
        {
            if (errno == EEXIST) continue;
            return failure(core::ErrorCode::IoError, "Cannot create temporary prefab file");
        }
        std::size_t offset = 0;
        bool ok = true;
        while (offset < contents.size())
        {
            const auto count = ::write(descriptor, contents.data() + offset, contents.size() - offset);
            if (count <= 0) { ok = false; break; }
            offset += static_cast<std::size_t>(count);
        }
        if (ok && ::fsync(descriptor) != 0) ok = false;
        const bool closed = ::close(descriptor) == 0;
        if (!ok || !closed)
        {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            return failure(core::ErrorCode::IoError, "Cannot flush temporary prefab file");
        }
        if (::rename(temporary.string().c_str(), file.string().c_str()) != 0)
        {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            return failure(core::ErrorCode::IoError, "Cannot atomically replace prefab file");
        }
        return core::Result<void>::success();
    }
    return failure(core::ErrorCode::IoError, "Cannot reserve temporary prefab file");
#endif
    return core::Result<void>::success();
}

struct FlatNode
{
    std::string key;
    std::optional<std::string> parentKey;
    PrefabNode node;
};

core::Result<void> expandPrefab(const PrefabLibrary& library, const PrefabId id, const std::string& prefix,
    const std::optional<std::string>& attachKey, std::vector<PrefabId>& stack, std::vector<FlatNode>& output)
{
    if (stack.size() >= MaxNestedDepth)
        return failure(core::ErrorCode::ValidationError, "Nested prefab depth exceeds 64");
    if (std::find(stack.begin(), stack.end(), id) != stack.end())
        return failure(core::ErrorCode::DependencyError, "Nested prefab cycle detected at PrefabId " + std::to_string(id));
    auto loaded = library.load(id);
    if (!loaded) return failure(loaded.error().code, loaded.error().message);
    const auto& prefab = loaded.value();
    stack.push_back(id);
    std::unordered_map<std::uint64_t, std::string> keys;
    std::unordered_set<std::uint64_t> emitted;
    std::function<core::Result<void>(const PrefabNode&)> emit = [&](const PrefabNode& node) -> core::Result<void> {
        const auto key = prefix + "p" + std::to_string(id) + "/n" + std::to_string(node.localId);
        std::optional<std::string> parentKey;
        if (node.parentLocalId != 0)
        {
            const auto parent = keys.find(node.parentLocalId);
            if (parent == keys.end()) return failure(core::ErrorCode::ValidationError, "Prefab node order is not parent-first");
            parentKey = parent->second;
        }
        else parentKey = attachKey;
        keys.emplace(node.localId, key);
        emitted.emplace(node.localId);
        output.push_back({key, parentKey, node});
        if (output.size() > MaxExpandedNodes) return failure(core::ErrorCode::ValidationError, "Expanded prefab exceeds entity limit");
        if (node.nestedPrefab != 0)
        {
            auto nested = expandPrefab(library, node.nestedPrefab, key + "/nested/", key, stack, output);
            if (!nested) return nested;
        }
        for (const auto& child : prefab.nodes)
            if (child.parentLocalId == node.localId)
            {
                auto result = emit(child);
                if (!result) return result;
            }
        return core::Result<void>::success();
    };
    const auto root = std::find_if(prefab.nodes.begin(), prefab.nodes.end(), [](const PrefabNode& node) {
        return node.parentLocalId == 0;
    });
    auto expanded = emit(*root);
    if (expanded)
        for (const auto& node : prefab.nodes)
            if (!emitted.contains(node.localId))
            {
                expanded = failure(core::ErrorCode::ValidationError, "Prefab hierarchy contains unreachable nodes");
                break;
            }
    stack.pop_back();
    return expanded;
}

core::Result<std::vector<FlatNode>> flatten(const PrefabLibrary& library, const PrefabId prefab)
{
    std::vector<FlatNode> nodes;
    std::vector<PrefabId> stack;
    auto result = expandPrefab(library, prefab, "", std::nullopt, stack, nodes);
    if (!result) return resultFailure<std::vector<FlatNode>>(result.error().code, result.error().message);
    return core::Result<std::vector<FlatNode>>::success(std::move(nodes));
}

core::Result<void> applyOverride(scene::Scene& target, const scene::EntityId entity,
    const scene::PrefabOverrideProperty property, const scene::PrefabOverrideValue& value)
{
    if (property == scene::PrefabOverrideProperty::Name)
    {
        const auto* name = std::get_if<std::string>(&value);
        if (!name) return failure(core::ErrorCode::TypeMismatch, "Prefab Name override requires a string");
        return target.setProperty(entity, "name", *name);
    }
    if (property == scene::PrefabOverrideProperty::Transform)
    {
        const auto* transform = std::get_if<scene::Transform>(&value);
        if (!transform) return failure(core::ErrorCode::TypeMismatch, "Prefab Transform override requires a Transform");
        return target.setTransform(entity, *transform);
    }
    if (property == scene::PrefabOverrideProperty::SpriteRenderer)
    {
        const auto* sprite = std::get_if<std::optional<scene::PrefabSpriteValue>>(&value);
        if (!sprite) return failure(core::ErrorCode::TypeMismatch, "Prefab SpriteRenderer override requires an optional sprite value");
        return *sprite ? target.setSpriteRenderer(entity, (*sprite)->texture, (*sprite)->visible)
                       : target.removeSpriteRenderer(entity);
    }
    return failure(core::ErrorCode::ValidationError, "Unknown prefab override property");
}

const scene::PrefabOverride* findOverride(const scene::PrefabInstanceInfo& instance, const std::string& key,
    const scene::PrefabOverrideProperty property)
{
    const auto found = std::find_if(instance.overrides.begin(), instance.overrides.end(), [&](const auto& value) {
        return value.sourceKey == key && value.property == property;
    });
    return found == instance.overrides.end() ? nullptr : &*found;
}
}

core::Result<void> validatePrefab(const PrefabAsset& prefab)
{
    if (prefab.id == 0 || prefab.id == std::numeric_limits<PrefabId>::max() || prefab.name.empty()
        || prefab.name.size() > 240 || !validUtf8(prefab.name) || prefab.nodes.empty() || prefab.nodes.size() > MaxPrefabNodes)
        return failure(core::ErrorCode::ValidationError, "Prefab ID, name, or node count is invalid");
    std::unordered_map<std::uint64_t, const PrefabNode*> nodes;
    nodes.reserve(prefab.nodes.size());
    std::size_t roots = 0;
    for (const auto& node : prefab.nodes)
    {
        if (node.localId == 0 || node.localId == std::numeric_limits<std::uint64_t>::max()
            || node.name.empty() || node.name.size() > 240 || !validUtf8(node.name)
            || (node.sprite && node.sprite->texture.value == 0))
            return failure(core::ErrorCode::ValidationError, "Prefab node ID, name, or sprite reference is invalid");
        if (!nodes.emplace(node.localId, &node).second)
            return failure(core::ErrorCode::AlreadyExists, "Prefab contains duplicate local entity IDs");
        if (node.parentLocalId == 0) ++roots;
    }
    if (roots != 1) return failure(core::ErrorCode::ValidationError, "Prefab must have exactly one root node");
    for (const auto& node : prefab.nodes)
        if (node.parentLocalId != 0 && !nodes.contains(node.parentLocalId))
            return failure(core::ErrorCode::NotFound, "Prefab node refers to a missing parent");
    std::unordered_map<std::uint64_t, std::uint8_t> state;
    std::unordered_map<std::uint64_t, std::size_t> depth;
    state.reserve(prefab.nodes.size());
    depth.reserve(prefab.nodes.size());
    for (const auto& node : prefab.nodes)
    {
        std::vector<std::uint64_t> path;
        auto current = node.localId;
        while (current != 0 && state[current] == 0)
        {
            state[current] = 1;
            path.push_back(current);
            current = nodes.at(current)->parentLocalId;
        }
        if (current != 0 && state[current] == 1)
            return failure(core::ErrorCode::ValidationError, "Prefab hierarchy contains a cycle");
        std::size_t currentDepth = current == 0 ? 0 : depth[current];
        for (auto it = path.rbegin(); it != path.rend(); ++it)
        {
            depth[*it] = ++currentDepth;
            state[*it] = 2;
            if (currentDepth > MaxPrefabHierarchyDepth)
                return failure(core::ErrorCode::ValidationError, "Prefab hierarchy depth exceeds 256");
        }
    }
    std::vector<scene::Entity> sceneEntities;
    sceneEntities.reserve(prefab.nodes.size());
    const auto componentBase = static_cast<scene::ComponentId>(prefab.nodes.size());
    for (std::size_t index = 0; index < prefab.nodes.size(); ++index)
    {
        const auto& node = prefab.nodes[index];
        const auto componentId = static_cast<scene::ComponentId>(index + 1);
        scene::Entity entity{node.localId, node.name,
            node.parentLocalId == 0 ? std::nullopt : std::optional<scene::EntityId>{node.parentLocalId},
            {{componentId, 1, node.transform}}, std::nullopt};
        if (node.sprite) entity.spriteRenderer = scene::SpriteRendererComponent{
            componentBase + componentId, 1, node.sprite->texture, node.sprite->visible};
        sceneEntities.push_back(std::move(entity));
    }
    auto sceneValidation = scene::restoreScene(1, prefab.name, std::move(sceneEntities));
    if (!sceneValidation) return failure(sceneValidation.error().code, "Invalid prefab nodes: " + sceneValidation.error().message);
    for (const auto& node : prefab.nodes)
        if (node.nestedPrefab == prefab.id)
            return failure(core::ErrorCode::DependencyError, "Prefab directly nests itself");
    return core::Result<void>::success();
}

core::Result<PrefabLibrary> PrefabLibrary::open(const std::filesystem::path& projectRoot)
{
    std::error_code error;
    const auto resolvedRoot = std::filesystem::canonical(projectRoot, error);
    if (error || !std::filesystem::is_directory(resolvedRoot, error) || error)
        return resultFailure<PrefabLibrary>(core::ErrorCode::IoError, "Project root is not an accessible directory");
    const auto directory = resolvedRoot / "Prefabs";
    const auto status = std::filesystem::symlink_status(directory, error);
    if (!error && std::filesystem::is_symlink(status))
        return resultFailure<PrefabLibrary>(core::ErrorCode::ValidationError, "Prefabs directory must not be a symlink");
    error.clear();
    std::filesystem::create_directory(directory, error);
    if (error && error != std::errc::file_exists)
        return resultFailure<PrefabLibrary>(core::ErrorCode::IoError, "Cannot create Prefabs directory: " + error.message());
    if (!std::filesystem::is_directory(directory, error) || error)
        return resultFailure<PrefabLibrary>(core::ErrorCode::ValidationError, "Prefabs path is not a directory");
    const auto resolvedDirectory = std::filesystem::canonical(directory, error);
    if (error) return resultFailure<PrefabLibrary>(core::ErrorCode::IoError, "Cannot resolve Prefabs directory");
    const auto relative = resolvedDirectory.lexically_relative(resolvedRoot);
    if (relative.empty() || relative.is_absolute() || *relative.begin() == "..")
        return resultFailure<PrefabLibrary>(core::ErrorCode::ValidationError, "Prefabs directory resolves outside the project root");
    return core::Result<PrefabLibrary>::success(PrefabLibrary(resolvedDirectory));
}

core::Result<void> PrefabLibrary::save(const PrefabAsset& prefab) const
{
    std::error_code pathError;
    if (std::filesystem::canonical(directory_, pathError) != directory_ || pathError)
        return failure(core::ErrorCode::ValidationError, "Prefab library directory no longer resolves to its project path");
    auto validation = validatePrefab(prefab);
    if (!validation) return validation;
    auto contents = serializePrefab(prefab);
    if (contents.size() > MaxPrefabFileBytes)
        return failure(core::ErrorCode::ValidationError, "Serialized prefab exceeds 16 MiB");
    return writeAtomically(prefabPath(directory_, prefab.id), contents);
}

core::Result<PrefabAsset> PrefabLibrary::load(const PrefabId id) const
{
    std::error_code pathError;
    if (std::filesystem::canonical(directory_, pathError) != directory_ || pathError)
        return resultFailure<PrefabAsset>(core::ErrorCode::ValidationError,
            "Prefab library directory no longer resolves to its project path");
    if (id == 0 || id == std::numeric_limits<PrefabId>::max())
        return resultFailure<PrefabAsset>(core::ErrorCode::InvalidArgument, "PrefabId is invalid");
    const auto file = prefabPath(directory_, id);
    std::error_code error;
    const auto size = std::filesystem::file_size(file, error);
    if (error) return resultFailure<PrefabAsset>(core::ErrorCode::IoError, "Cannot inspect prefab file: " + error.message());
    if (size > MaxPrefabFileBytes)
        return resultFailure<PrefabAsset>(core::ErrorCode::ValidationError, "Prefab file exceeds 16 MiB");
    std::ifstream input(file, std::ios::binary);
    if (!input) return resultFailure<PrefabAsset>(core::ErrorCode::IoError, "Cannot open prefab file");
    std::string line;
    const auto nextLine = [&input, &line] {
        if (!std::getline(input, line)) return false;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        return true;
    };
    if (!nextLine()) return resultFailure<PrefabAsset>(core::ErrorCode::ParseError, "Prefab file is empty");
    std::istringstream header(line);
    header.imbue(std::locale::classic());
    std::string magic;
    unsigned version = 0;
    if (!(header >> magic >> version) || hasExtra(header) || magic != "ASTRO_PREFAB")
        return resultFailure<PrefabAsset>(core::ErrorCode::ParseError, "Invalid prefab file header");
    if (version != 1) return resultFailure<PrefabAsset>(core::ErrorCode::VersionMismatch, "Unsupported prefab file version");
    if (!nextLine()) return resultFailure<PrefabAsset>(core::ErrorCode::ParseError, "Prefab metadata is missing");
    PrefabAsset prefab;
    std::istringstream metadata(line);
    metadata.imbue(std::locale::classic());
    std::string marker;
    if (!(metadata >> marker >> prefab.id >> std::quoted(prefab.name)) || hasExtra(metadata) || marker != "PREFAB"
        || prefab.id != id)
        return resultFailure<PrefabAsset>(core::ErrorCode::ParseError, "Invalid prefab metadata");
    bool ended = false;
    while (nextLine())
    {
        if (line == "END") { ended = true; break; }
        std::istringstream record(line);
        record.imbue(std::locale::classic());
        PrefabNode node;
        unsigned hasSprite = 0, visible = 0;
        std::uint64_t spriteId = 0;
        if (!(record >> marker) || marker != "NODE"
            || !(record >> node.localId >> node.parentLocalId >> std::quoted(node.name)
                >> node.transform.position.x >> node.transform.position.y >> node.transform.position.z
                >> node.transform.rotationDegrees.x >> node.transform.rotationDegrees.y >> node.transform.rotationDegrees.z
                >> node.transform.scale.x >> node.transform.scale.y >> node.transform.scale.z >> hasSprite)
            || hasSprite > 1)
            return resultFailure<PrefabAsset>(core::ErrorCode::ParseError, "Invalid prefab node record");
        if (hasSprite)
        {
            if (!(record >> spriteId >> visible) || visible > 1)
                return resultFailure<PrefabAsset>(core::ErrorCode::ParseError, "Invalid prefab SpriteRenderer data");
            node.sprite = scene::PrefabSpriteValue{{spriteId}, visible != 0};
        }
        if (!(record >> node.nestedPrefab) || hasExtra(record))
            return resultFailure<PrefabAsset>(core::ErrorCode::ParseError, "Invalid prefab nested reference");
        if (prefab.nodes.size() == MaxPrefabNodes)
            return resultFailure<PrefabAsset>(core::ErrorCode::ValidationError, "Prefab exceeds node-count limit");
        prefab.nodes.push_back(std::move(node));
    }
    if (!ended) return resultFailure<PrefabAsset>(core::ErrorCode::ParseError, "Prefab terminator is missing");
    while (nextLine())
        if (!line.empty() && line.find_first_not_of(" \t\r") != std::string::npos)
            return resultFailure<PrefabAsset>(core::ErrorCode::ParseError, "Unexpected data after prefab terminator");
    auto validation = validatePrefab(prefab);
    if (!validation) return resultFailure<PrefabAsset>(validation.error().code, validation.error().message);
    return core::Result<PrefabAsset>::success(std::move(prefab));
}

core::Result<PrefabAsset> capturePrefab(const scene::Scene& source, const scene::EntityId root,
    const PrefabId id, std::string name)
{
    if (!source.find(root)) return resultFailure<PrefabAsset>(core::ErrorCode::NotFound, "Prefab source entity does not exist");
    std::vector<scene::EntityId> ordered{root};
    for (std::size_t index = 0; index < ordered.size(); ++index)
        for (const auto& entity : source.entities())
            if (entity.parent == ordered[index]) ordered.push_back(entity.id);
    std::unordered_map<scene::EntityId, std::uint64_t> localIds;
    for (std::size_t index = 0; index < ordered.size(); ++index) localIds.emplace(ordered[index], index + 1);
    PrefabAsset prefab{id, std::move(name), {}};
    for (const auto entityId : ordered)
    {
        const auto* entity = source.find(entityId);
        const auto* transform = source.transform(entityId);
        if (!entity || !transform) return resultFailure<PrefabAsset>(core::ErrorCode::InvalidState, "Prefab source is incomplete");
        PrefabNode node;
        node.localId = localIds.at(entityId);
        node.parentLocalId = entityId == root ? 0 : localIds.at(*entity->parent);
        node.name = entity->name;
        node.transform = transform->value;
        if (const auto* sprite = source.spriteRenderer(entityId)) node.sprite = scene::PrefabSpriteValue{sprite->texture, sprite->visible};
        prefab.nodes.push_back(std::move(node));
    }
    auto validation = validatePrefab(prefab);
    if (!validation) return resultFailure<PrefabAsset>(validation.error().code, validation.error().message);
    return core::Result<PrefabAsset>::success(std::move(prefab));
}

core::Result<scene::EntityId> instantiatePrefab(const PrefabLibrary& library, scene::Scene& target,
    const PrefabId prefab, const std::optional<scene::EntityId> parent)
{
    if (parent && !target.find(*parent))
        return resultFailure<scene::EntityId>(core::ErrorCode::NotFound, "Prefab instance parent entity does not exist");
    auto flattened = flatten(library, prefab);
    if (!flattened) return resultFailure<scene::EntityId>(flattened.error().code, flattened.error().message);
    scene::Scene working = target;
    scene::PrefabInstanceInfo instance;
    instance.prefab = prefab;
    scene::EntityId rootEntity = 0;
    for (const auto& flat : flattened.value())
    {
        std::optional<scene::EntityId> actualParent = parent;
        if (flat.parentKey)
        {
            const auto found = instance.sourceToEntity.find(*flat.parentKey);
            if (found == instance.sourceToEntity.end())
                return resultFailure<scene::EntityId>(core::ErrorCode::ValidationError, "Expanded prefab has an unresolved parent");
            actualParent = found->second;
        }
        auto created = working.createEntity(flat.node.name, actualParent);
        if (!created) return resultFailure<scene::EntityId>(created.error().code, created.error().message);
        auto transform = working.setTransform(created.value(), flat.node.transform);
        if (!transform) return resultFailure<scene::EntityId>(transform.error().code, transform.error().message);
        if (flat.node.sprite)
        {
            auto sprite = working.setSpriteRenderer(created.value(), flat.node.sprite->texture, flat.node.sprite->visible);
            if (!sprite) return resultFailure<scene::EntityId>(sprite.error().code, sprite.error().message);
        }
        if (rootEntity == 0) rootEntity = created.value();
        instance.sourceToEntity.emplace(flat.key, created.value());
    }
    instance.rootEntity = rootEntity;
    auto recorded = working.setPrefabInstance(std::move(instance));
    if (!recorded) return resultFailure<scene::EntityId>(recorded.error().code, recorded.error().message);
    target = std::move(working);
    return core::Result<scene::EntityId>::success(rootEntity);
}

core::Result<void> setPrefabOverride(scene::Scene& target, const scene::EntityId instanceRoot,
    std::string sourceKey, const scene::PrefabOverrideProperty property, scene::PrefabOverrideValue value)
{
    scene::Scene working = target;
    auto* instance = working.prefabInstance(instanceRoot);
    if (!instance) return failure(core::ErrorCode::NotFound, "Prefab instance does not exist");
    const auto mapped = instance->sourceToEntity.find(sourceKey);
    if (mapped == instance->sourceToEntity.end()) return failure(core::ErrorCode::NotFound, "Prefab source entity does not exist");
    auto applied = applyOverride(working, mapped->second, property, value);
    if (!applied) return applied;
    auto existing = std::find_if(instance->overrides.begin(), instance->overrides.end(), [&](const auto& entry) {
        return entry.sourceKey == sourceKey && entry.property == property;
    });
    scene::PrefabOverride overrideValue{std::move(sourceKey), property, std::move(value)};
    if (existing == instance->overrides.end()) instance->overrides.push_back(std::move(overrideValue));
    else *existing = std::move(overrideValue);
    auto validation = working.setPrefabInstance(*instance);
    if (!validation) return validation;
    target = std::move(working);
    return core::Result<void>::success();
}

core::Result<void> updatePrefabInstance(const PrefabLibrary& library, scene::Scene& target,
    const scene::EntityId instanceRoot)
{
    const auto* originalInstance = target.prefabInstance(instanceRoot);
    if (!originalInstance) return failure(core::ErrorCode::NotFound, "Prefab instance does not exist");
    auto flattened = flatten(library, originalInstance->prefab);
    if (!flattened) return failure(flattened.error().code, flattened.error().message);
    const auto& desired = flattened.value();
    if (desired.empty()) return failure(core::ErrorCode::ValidationError, "Updated prefab has no expanded nodes");
    for (const auto& overrideValue : originalInstance->overrides)
    {
        const bool sourceExists = std::any_of(desired.begin(), desired.end(), [&](const auto& node) {
            return node.key == overrideValue.sourceKey;
        });
        if (!sourceExists)
            return failure(core::ErrorCode::InvalidState, "Prefab update would orphan an override at " + overrideValue.sourceKey);
    }

    scene::Scene working = target;
    const auto* instanceBefore = working.prefabInstance(instanceRoot);
    const auto oldMap = instanceBefore->sourceToEntity;
    const auto oldOverrides = instanceBefore->overrides;
    const auto externalParent = working.find(instanceRoot)->parent;
    scene::PrefabInstanceInfo updated;
    updated.rootEntity = instanceRoot;
    updated.prefab = instanceBefore->prefab;
    updated.overrides = oldOverrides;

    for (std::size_t index = 0; index < desired.size(); ++index)
    {
        const auto& flat = desired[index];
        std::optional<scene::EntityId> parent = externalParent;
        if (flat.parentKey)
        {
            const auto resolved = updated.sourceToEntity.find(*flat.parentKey);
            if (resolved == updated.sourceToEntity.end())
                return failure(core::ErrorCode::ValidationError, "Updated prefab has an unresolved parent");
            parent = resolved->second;
        }
        scene::EntityId entityId = 0;
        const auto prior = oldMap.find(flat.key);
        if (index == 0) entityId = instanceRoot;
        else if (prior != oldMap.end()) entityId = prior->second;
        else
        {
            auto created = working.createEntity(flat.node.name, parent);
            if (!created) return failure(created.error().code, created.error().message);
            entityId = created.value();
        }
        if (!working.find(entityId)) return failure(core::ErrorCode::NotFound, "Mapped prefab entity no longer exists");
        auto parentResult = working.reparent(entityId, parent);
        if (!parentResult) return parentResult;
        auto nameResult = working.setProperty(entityId, "name", flat.node.name);
        if (!nameResult) return nameResult;
        auto transformResult = working.setTransform(entityId, flat.node.transform);
        if (!transformResult) return transformResult;
        auto spriteResult = flat.node.sprite
            ? working.setSpriteRenderer(entityId, flat.node.sprite->texture, flat.node.sprite->visible)
            : working.removeSpriteRenderer(entityId);
        if (!spriteResult && spriteResult.error().code != core::ErrorCode::NotFound) return spriteResult;
        updated.sourceToEntity.emplace(flat.key, entityId);
        if (const auto* value = findOverride(*instanceBefore, flat.key, scene::PrefabOverrideProperty::Name))
            if (auto result = applyOverride(working, entityId, value->property, value->value); !result) return result;
        if (const auto* value = findOverride(*instanceBefore, flat.key, scene::PrefabOverrideProperty::Transform))
            if (auto result = applyOverride(working, entityId, value->property, value->value); !result) return result;
        if (const auto* value = findOverride(*instanceBefore, flat.key, scene::PrefabOverrideProperty::SpriteRenderer))
            if (auto result = applyOverride(working, entityId, value->property, value->value); !result) return result;
    }

    std::vector<scene::EntityId> obsolete;
    for (const auto& [key, entity] : oldMap)
        if (!updated.sourceToEntity.contains(key) && entity != instanceRoot) obsolete.push_back(entity);
    while (!obsolete.empty())
    {
        const auto before = obsolete.size();
        for (auto it = obsolete.begin(); it != obsolete.end();)
        {
            auto erased = working.erase(*it, false);
            if (erased) it = obsolete.erase(it);
            else if (erased.error().code == core::ErrorCode::NotFound) it = obsolete.erase(it);
            else ++it;
        }
        if (obsolete.size() == before)
            return failure(core::ErrorCode::InvalidState, "Prefab update cannot remove an entity with non-prefab children");
    }
    auto recorded = working.setPrefabInstance(std::move(updated));
    if (!recorded) return recorded;
    target = std::move(working);
    return core::Result<void>::success();
}
}

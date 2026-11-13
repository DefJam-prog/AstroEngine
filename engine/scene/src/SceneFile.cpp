#include "astro/scene/SceneFile.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <string_view>
#include <utility>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace astro::scene
{
namespace
{
constexpr std::uintmax_t MaxSceneFileBytes = 16u * 1024u * 1024u;
constexpr std::size_t MaxSceneEntities = 100000;

core::Result<Scene> fileError(const core::ErrorCode code, std::string message)
{
    return core::Result<Scene>::failure({code, std::move(message)});
}

core::Result<void> writeError(const core::ErrorCode code, std::string message)
{
    return core::Result<void>::failure({code, std::move(message)});
}

bool hasExtra(std::istringstream& input)
{
    std::string extra;
    return static_cast<bool>(input >> extra);
}

std::string serialize(const Scene& scene)
{
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::setprecision(std::numeric_limits<double>::max_digits10);
    output << "ASTRO_SCENE 3\nSCENE " << scene.id() << ' ' << std::quoted(scene.name()) << '\n';
    for (const auto& entity : scene.entities())
        output << "ENTITY " << entity.id << ' ' << entity.parent.value_or(0) << ' '
               << std::quoted(entity.name) << '\n';
    for (const auto& entity : scene.entities())
        for (const auto& component : entity.components)
            output << "COMPONENT " << component.id << ' ' << entity.id << ' '
                   << std::quoted("astro.Transform") << ' ' << component.schemaVersion << ' '
                   << component.value.position.x << ' ' << component.value.position.y << ' ' << component.value.position.z << ' '
                   << component.value.rotationDegrees.x << ' ' << component.value.rotationDegrees.y << ' '
                   << component.value.rotationDegrees.z << ' ' << component.value.scale.x << ' '
                   << component.value.scale.y << ' ' << component.value.scale.z << '\n';
    for (const auto& entity : scene.entities())
        if (entity.spriteRenderer)
            output << "COMPONENT " << entity.spriteRenderer->id << ' ' << entity.id << ' '
                   << std::quoted("astro.SpriteRenderer") << ' ' << entity.spriteRenderer->schemaVersion << ' '
                   << entity.spriteRenderer->texture.value << ' ' << (entity.spriteRenderer->visible ? 1 : 0) << '\n';
    output << "END\n";
    return output.str();
}

std::string ioMessage(const char* action, const std::filesystem::path& file)
{
    return std::string(action) + ": " + file.string();
}

core::Result<void> publishAtomically(const std::filesystem::path& target, const std::string& contents)
{
    static std::atomic<std::uint64_t> sequence{0};
    std::filesystem::path temporary;
#ifdef _WIN32
    HANDLE handle = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 32; ++attempt)
    {
        temporary = target;
        temporary += L".tmp." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(GetTickCount64())
            + L"." + std::to_wstring(sequence.fetch_add(1, std::memory_order_relaxed));
        handle = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
            FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_WRITE_THROUGH, nullptr);
        if (handle != INVALID_HANDLE_VALUE) break;
        const auto error = GetLastError();
        if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS)
            return writeError(core::ErrorCode::IoError, "Cannot create temporary scene file (Windows error "
                + std::to_string(error) + ")");
    }
    if (handle == INVALID_HANDLE_VALUE)
        return writeError(core::ErrorCode::IoError, "Cannot reserve a unique temporary scene file");

    DWORD written = 0;
    const bool ok = WriteFile(handle, contents.data(), static_cast<DWORD>(contents.size()), &written, nullptr)
        && written == static_cast<DWORD>(contents.size()) && FlushFileBuffers(handle);
    const auto writeCode = ok ? ERROR_SUCCESS : GetLastError();
    const bool closed = CloseHandle(handle) != 0;
    if (!ok || !closed)
    {
        const auto code = ok ? GetLastError() : writeCode;
        DeleteFileW(temporary.c_str());
        return writeError(core::ErrorCode::IoError, "Cannot flush temporary scene file (Windows error "
            + std::to_string(code) + ")");
    }
    if (!MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        const auto code = GetLastError();
        DeleteFileW(temporary.c_str());
        return writeError(core::ErrorCode::IoError, "Cannot atomically replace scene file (Windows error "
            + std::to_string(code) + ")");
    }
#else
    for (int attempt = 0; attempt < 32; ++attempt)
    {
        temporary = target;
        temporary += ".tmp." + std::to_string(sequence.fetch_add(1, std::memory_order_relaxed));
        const int descriptor = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (descriptor < 0)
        {
            if (errno == EEXIST) continue;
            return writeError(core::ErrorCode::IoError, ioMessage("Cannot create temporary scene file", temporary));
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
            return writeError(core::ErrorCode::IoError, ioMessage("Cannot flush temporary scene file", temporary));
        }
        if (::rename(temporary.string().c_str(), target.string().c_str()) != 0)
        {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            return writeError(core::ErrorCode::IoError, ioMessage("Cannot atomically replace scene file", target));
        }
        return core::Result<void>::success();
    }
    return writeError(core::ErrorCode::IoError, "Cannot reserve a unique temporary scene file");
#endif
    return core::Result<void>::success();
}
}

core::Result<void> saveSceneFile(const std::filesystem::path& file, const Scene& scene)
{
    if (file.empty()) return writeError(core::ErrorCode::InvalidArgument, "Scene file path is empty");
    auto validation = restoreScene(scene.id(), scene.name(), scene.entities());
    if (!validation) return writeError(validation.error().code, validation.error().message);
    const auto contents = serialize(scene);
    if (contents.size() > MaxSceneFileBytes)
        return writeError(core::ErrorCode::ValidationError, "Serialized scene exceeds 16 MiB");
    return publishAtomically(file, contents);
}

core::Result<Scene> loadSceneFile(const std::filesystem::path& file)
{
    if (file.empty()) return fileError(core::ErrorCode::InvalidArgument, "Scene file path is empty");
    std::error_code fsError;
    const auto size = std::filesystem::file_size(file, fsError);
    if (fsError) return fileError(core::ErrorCode::IoError, "Cannot inspect scene file: " + fsError.message());
    if (size > MaxSceneFileBytes) return fileError(core::ErrorCode::ValidationError, "Scene file exceeds 16 MiB");

    std::ifstream input(file, std::ios::binary);
    if (!input) return fileError(core::ErrorCode::IoError, "Cannot open scene file");
    std::string line;
    const auto nextLine = [&input, &line] {
        if (!std::getline(input, line)) return false;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        return true;
    };
    std::uint32_t version = 0;
    if (!nextLine()) return fileError(core::ErrorCode::ParseError, "Scene file is empty");
    std::istringstream header(line);
    header.imbue(std::locale::classic());
    std::string magic;
    if (!(header >> magic >> version) || hasExtra(header) || magic != "ASTRO_SCENE")
        return fileError(core::ErrorCode::ParseError, "Invalid scene file header");
    if (version != 1 && version != 2 && version != 3)
        return fileError(core::ErrorCode::VersionMismatch, "Unsupported scene file version");

    if (!nextLine()) return fileError(core::ErrorCode::ParseError, "Scene metadata is missing");
    std::istringstream sceneLine(line);
    sceneLine.imbue(std::locale::classic());
    std::string marker, name;
    SceneId sceneId = 0;
    if (!(sceneLine >> marker >> sceneId >> std::quoted(name)) || marker != "SCENE" || hasExtra(sceneLine))
        return fileError(core::ErrorCode::ParseError, "Invalid scene metadata");

    std::vector<Entity> entities;
    entities.reserve(static_cast<std::size_t>(std::min<std::uintmax_t>(size / 24, MaxSceneEntities)));
    bool ended = false;
    while (nextLine())
    {
        if (line == "END") { ended = true; break; }
        std::istringstream record(line);
        record.imbue(std::locale::classic());
        if (!(record >> marker)) return fileError(core::ErrorCode::ParseError, "Invalid scene record");
        if (marker == "ENTITY")
        {
            Entity entity;
            EntityId parent = 0;
            if (!(record >> entity.id >> parent >> std::quoted(entity.name)) || hasExtra(record))
                return fileError(core::ErrorCode::ParseError, "Invalid entity record");
            if (entities.size() == MaxSceneEntities)
                return fileError(core::ErrorCode::ValidationError, "Scene exceeds the entity-count limit");
            if (parent != 0) entity.parent = parent;
            entities.push_back(std::move(entity));
        }
        else if (marker == "COMPONENT" && version >= 2)
        {
            ComponentId componentId = 0;
            EntityId ownerId = 0;
            std::string type;
            std::uint32_t componentVersion = 0;
            if (!(record >> componentId >> ownerId >> std::quoted(type) >> componentVersion))
                return fileError(core::ErrorCode::ParseError, "Invalid component record");
            const auto owner = std::find_if(entities.begin(), entities.end(), [ownerId](const Entity& entity) {
                return entity.id == ownerId;
            });
            if (owner == entities.end())
                return fileError(core::ErrorCode::NotFound, "Component owner entity does not exist");
            if (type == "astro.Transform")
            {
                Transform transform;
                if (!(record >> transform.position.x >> transform.position.y >> transform.position.z
                    >> transform.rotationDegrees.x >> transform.rotationDegrees.y >> transform.rotationDegrees.z
                    >> transform.scale.x >> transform.scale.y >> transform.scale.z) || hasExtra(record))
                    return fileError(core::ErrorCode::ParseError, "Invalid Transform component record");
                owner->components.push_back({componentId, componentVersion, transform});
            }
            else if (type == "astro.SpriteRenderer" && version >= 3)
            {
                if (owner->spriteRenderer)
                    return fileError(core::ErrorCode::AlreadyExists, "Entity contains duplicate SpriteRenderer components");
                std::uint64_t assetId = 0;
                unsigned visible = 0;
                if (!(record >> assetId >> visible) || visible > 1 || hasExtra(record))
                    return fileError(core::ErrorCode::ParseError, "Invalid SpriteRenderer component record");
                owner->spriteRenderer = SpriteRendererComponent{componentId, componentVersion, {assetId}, visible != 0};
            }
            else
                return fileError(core::ErrorCode::ValidationError, "Unknown component type: " + type);
        }
        else
            return fileError(core::ErrorCode::ParseError, "Unknown or unsupported scene record");
    }
    if (!ended) return fileError(core::ErrorCode::ParseError, "Scene terminator is missing");
    while (nextLine())
        if (!line.empty() && line.find_first_not_of(" \t\r") != std::string::npos)
            return fileError(core::ErrorCode::ParseError, "Unexpected data after scene terminator");
    if (input.bad()) return fileError(core::ErrorCode::IoError, "Error while reading scene file");
    if (version == 1)
    {
        ComponentId nextComponentId = 1;
        for (auto& entity : entities)
        {
            if (nextComponentId == std::numeric_limits<ComponentId>::max())
                return fileError(core::ErrorCode::ValidationError, "Component ID space is exhausted during v1 migration");
            entity.components.push_back({nextComponentId++, 1, {}});
        }
    }
    return restoreScene(sceneId, std::move(name), std::move(entities));
}
}
